#pragma once

#include "auto/tl/cocoon_api.h"
#include "runners/BaseRunner.hpp"
#include "runners/smartcontracts/ClientContract.hpp"
#include "td/utils/Random.h"
#include "td/utils/SharedSlice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/port/Clocks.h"

namespace cocoon {

class ClientRunner;

class ClientProxyInfo : public std::enable_shared_from_this<ClientProxyInfo> {
 public:
  ClientProxyInfo(ClientRunner *runner, td::Bits256 proxy_public_key, block::StdAddress proxy_sc_address);
  ~ClientProxyInfo() {
    if (sc_) {
      sc_->set_callback(nullptr);
      sc_->unsubscribe_from_updates();
    }
  }

  std::shared_ptr<ClientProxyInfo> shared_ptr() {
    return shared_from_this();
  }

  const auto &proxy_public_key() const {
    return sc_->proxy_public_key();
  }
  const auto &proxy_sc_address() const {
    return sc_->proxy_sc_address();
  }
  const auto &client_sc_address() const {
    return sc_->address();
  }
  const auto &sc() const {
    return sc_;
  }
  auto sc_request_running() const {
    return sc_request_running_;
  }
  auto last_request_ago() const {
    return td::Clocks::monotonic() - last_request_at_monotonic_;
  }
  const auto &exp_sc_secret_hash() const {
    return exp_sc_secret_hash_;
  }
  bool exp_sc_is_inited() const {
    return sc_->is_inited() && exp_sc_state_ >= 0;
  }
  auto exp_sc_state() const {
    return exp_sc_state_;
  }
  auto exp_sc_unlock_ts() const {
    return exp_sc_unlock_ts_;
  }
  auto outdated_for() {
    if (outdated_since_ == 0) {
      outdated_since_ = td::Clocks::monotonic();
    }
    return td::Clocks::monotonic() - outdated_since_;
  }
  bool need_ton_top_up() const {
    return exp_sc_ton_balance_ < to_nano(0.6);
  }
  bool can_charge() const {
    return !sc_request_running_ && exp_sc_tokens_used_ < tokens_used_proxy_committed_to_blockchain_;
  }
  bool exp_sc_is_closing() const {
    return exp_sc_state_ >= 1;
  }
  bool exp_sc_is_closed() const {
    return exp_sc_state_ >= 2;
  }
  auto exp_sc_stake() const {
    return exp_sc_stake_;
  }

  void update_tokens_used(td::int64 tokens_used) {
    if (tokens_used_proxy_max_ < tokens_used) {
      tokens_used_proxy_max_ = tokens_used;
    }
  }
  void update_tokens_committed_to_db(td::int64 tokens_used) {
    if (tokens_used_proxy_committed_to_db_ < tokens_used) {
      tokens_used_proxy_committed_to_db_ = tokens_used;
    }
  }

  void update_from_sc() {
    if (!sc_->is_inited() || !sc_->is_started()) {
      return;
    }
    sc_ton_balance_ = sc_->balance();
    sc_state_ = sc_->state();
    sc_stake_ = sc_->stake();
    sc_tokens_used_ = sc_->tokens_used();
    sc_balance_ = sc_->balance_for_requests();
    sc_secret_hash_ = sc_->secret_hash();
    sc_unlock_ts_ = sc_->unlock_ts();
    auto price_per_token = sc_->runner_config()->root_contract_config->price_per_token();
    sc_tokens_payed_ = sc_tokens_used_ + safe_div(sc_balance_, price_per_token);

    if (!sc_request_running_) {
      exp_sc_ton_balance_ = sc_ton_balance_;
      exp_sc_state_ = std::max(exp_sc_state_, sc_state_);
      exp_sc_stake_ = std::max(exp_sc_stake_, sc_stake_);
      exp_sc_tokens_used_ = std::max(exp_sc_tokens_used_, sc_tokens_used_);
      exp_sc_secret_hash_ = sc_secret_hash_;
      exp_sc_unlock_ts_ = std::max(exp_sc_unlock_ts_, sc_unlock_ts_);
      exp_sc_tokens_payed_ = std::max(exp_sc_tokens_payed_, sc_tokens_payed_);
    }
  }

  td::int64 exp_available_tokens() const {
    auto v = std::min(exp_sc_tokens_payed_ - tokens_used_proxy_max_,
                      exp_sc_tokens_used_ +
                          safe_div(exp_sc_stake_, sc_->runner_config()->root_contract_config->price_per_token()) -
                          tokens_used_proxy_max_);
    return v > 0 ? v : 0;
  }

  td::Ref<vm::Cell> run_top_up(td::int64 coins) {
    auto price_per_token = sc_->runner_config()->root_contract_config->price_per_token();
    if (price_per_token > 0) {
      coins -= coins % price_per_token;
      exp_sc_tokens_payed_ += coins / price_per_token;
    } else {
      exp_sc_tokens_payed_ = 100000000000000LL;
    }

    CHECK(!sc_request_running_);
    sc_request_running_ = true;
    sc_request_qid_ = td::Random::fast_uint64();
    return sc_->create_topup_message(coins, sc_request_qid_);
  }
  td::Ref<vm::Cell> run_close() {
    CHECK(!sc_request_running_);
    exp_sc_state_ = std::max(sc_state_, 1);
    sc_request_running_ = true;
    sc_request_qid_ = td::Random::fast_uint64();
    return sc_->create_request_refund_message(sc_request_qid_);
  }
  td::Ref<vm::Cell> run_change_secret_hash(const td::Bits256 &secret_hash) {
    CHECK(!sc_request_running_);
    sc_request_running_ = true;
    exp_sc_secret_hash_ = secret_hash;
    sc_request_qid_ = td::Random::fast_uint64();
    return sc_->create_change_secret_hash_message(secret_hash, sc_request_qid_);
  }
  td::Ref<vm::Cell> run_charge();
  td::Ref<vm::Cell> run_withdraw() {
    CHECK(!sc_request_running_);
    exp_sc_tokens_payed_ = std::min(
        exp_sc_tokens_payed_,
        exp_sc_tokens_used_ + safe_div(exp_sc_stake_, sc_->runner_config()->root_contract_config->price_per_token()));

    sc_request_running_ = true;
    sc_request_qid_ = td::Random::fast_uint64();
    return sc_->create_withdraw_message(sc_request_qid_);
  }

  void sc_request_completed(const block::StdAddress &source, td::uint32 op, td::uint64 qid) {
    if (sc_request_running_ && sc_request_qid_ == qid && source.workchain == sc_->owner_address().workchain &&
        source.addr == sc_->owner_address().addr) {
      sc_request_running_ = false;
      sc_request_qid_ = 0;
    }
  }

  ClientCheckResult check();

  void process_signed_payment_data(cocoon_api::proxy_SignedPayment &data);
  void store_stats(td::StringBuilder &sb);
  void store_stats(SimpleJsonSerializer &sb);

  auto requests_running() const {
    return requests_running_;
  }
  void request_started() {
    requests_running_++;
    last_request_at_monotonic_ = td::Clocks::monotonic();
  }
  void request_finished() {
    requests_running_--;
  }

 private:
  std::shared_ptr<ClientContract> sc_;
  td::int64 sc_ton_balance_{0};
  td::int32 sc_state_{-1};
  td::int64 sc_stake_{0};
  td::int64 sc_tokens_used_{0};
  td::int64 sc_balance_{0};
  td::Bits256 sc_secret_hash_ = td::Bits256::zero();
  td::int32 sc_unlock_ts_{0};
  td::int64 sc_tokens_payed_{0};

  td::int64 exp_sc_ton_balance_{0};
  td::int32 exp_sc_state_{-1};
  td::int64 exp_sc_stake_{0};
  td::int64 exp_sc_tokens_used_{0};
  td::Bits256 exp_sc_secret_hash_ = td::Bits256::zero();
  td::int32 exp_sc_unlock_ts_{0};
  td::int64 exp_sc_tokens_payed_{0};

  td::int64 tokens_used_proxy_max_{0};
  td::int64 tokens_used_proxy_committed_to_db_{0};
  td::int64 tokens_used_proxy_committed_to_blockchain_{0};
  td::UniqueSlice signed_charge_message_;

  td::int64 exp_tokens_committed_to_db_{0};

  double outdated_since_{0};
  double last_request_at_monotonic_{0};
  td::int64 requests_running_{0};
  bool sc_request_running_{false};
  td::uint64 sc_request_qid_{0};
};

}  // namespace cocoon
