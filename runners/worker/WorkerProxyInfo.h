#pragma once

#include "runners/BaseRunner.hpp"
#include "runners/smartcontracts/WorkerContract.hpp"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/buffer.h"
#include "td/utils/port/Clocks.h"

namespace cocoon {

class WorkerRunner;

class WorkerProxyInfo {
 public:
  WorkerProxyInfo(WorkerRunner *runner, const td::Bits256 &proxy_public_key, const block::StdAddress &proxy_sc_address);
  ~WorkerProxyInfo() {
    if (sc_) {
      sc_->set_callback(nullptr);
      sc_->unsubscribe_from_updates();
    }
  }

  const auto &proxy_public_key() const {
    return sc_->proxy_public_key();
  }
  const auto &proxy_sc_address() const {
    return sc_->proxy_sc_address();
  }
  const auto &worker_sc_address() const {
    return sc_->address();
  }

  bool is_inited() const {
    return sc_->is_inited();
  }
  bool is_started() const {
    return sc_->is_started();
  }
  bool sc_request_is_running() const {
    return sc_request_running_;
  }

  auto earned_tokens_committed_to_blockchain() const {
    return tokens_committed_to_blockchain_;
  }

  auto earned_tokens_committed_to_proxy_db() const {
    return tokens_committed_to_proxy_db_;
  }

  auto earned_tokens_max_known() const {
    return tokens_max_known_;
  }

  auto exp_tokens_cashed_out() const {
    return std::max(sc_->tokens_processed(), exp_tokens_processed_);
  }

  auto tokens_cashed_out() const {
    return sc_->tokens_processed();
  }

  td::int64 to_payout() const {
    auto tokens = std::max(sc_->tokens_processed(), exp_tokens_processed_);
    return tokens < tokens_committed_to_blockchain_ ? tokens_committed_to_blockchain_ - tokens : 0;
  }

  bool is_closed() const {
    return sc_->state() == 2;
  }

  const auto &sc() const {
    return sc_;
  }

  double time_since_close_started() {
    if (!time_since_close_started_) {
      time_since_close_started_ = td::Timestamp::now();
    }
    return -time_since_close_started_.in();
  }

  td::Ref<vm::Cell> run_payout() {
    sc_request_running_ = true;
    exp_tokens_processed_ = tokens_committed_to_blockchain_;
    return sc_->repack_signed_pay_message(payout_message_.as_slice(), sc_->runner()->cocoon_wallet()->address(),
                                          &sc_request_qid_);
  }

  bool update_payment_info(const cocoon_api::proxy_signedPayment &payment);
  bool update_payment_info(ton::tl_object_ptr<cocoon_api::proxy_SignedPayment> payment) {
    if (!payment) {
      return false;
    }
    if (payment->get_id() == cocoon_api::proxy_signedPayment::ID) {
      return update_payment_info(static_cast<const cocoon_api::proxy_signedPayment &>(*payment));
    } else {
      return false;
    }
  }
  bool update_tokens_committed_to_proxy_db(td::int64 tokens) {
    if (tokens > tokens_committed_to_proxy_db_) {
      tokens_committed_to_proxy_db_ = tokens;
      update_tokens_max_known(tokens);
    }
    return tokens == tokens_committed_to_proxy_db_;
  }

  bool update_tokens_max_known(td::int64 tokens) {
    if (tokens > tokens_max_known_) {
      tokens_max_known_ = tokens;
    }
    return tokens == tokens_max_known_;
  }

  td::BufferSlice export_difference_with_db(td::int64 from_tokens) {
    return td::BufferSlice();
  }

  void sc_request_completed(const block::StdAddress &source, td::uint32 op, td::uint64 qid) {
    if (sc_request_running_ && sc_request_qid_ == qid &&
        source.workchain == sc_->runner()->cocoon_wallet_address().workchain &&
        source.addr == sc_->runner()->cocoon_wallet_address().addr) {
      sc_request_running_ = false;
      sc_request_qid_ = 0;
    }
  }

  void store_stats(td::StringBuilder &sb);
  void store_stats(SimpleJsonSerializer &sb);

  ClientCheckResult check();

  void received_request_from_proxy() {
    last_request_at_ = td::Clocks::monotonic();
  }

 private:
  std::shared_ptr<WorkerContract> sc_;
  td::UniqueSlice payout_message_;
  td::int64 tokens_committed_to_blockchain_ = 0;
  td::int64 tokens_committed_to_proxy_db_ = 0;
  td::int64 tokens_max_known_ = 0;
  td::int64 exp_tokens_processed_ = 0;
  td::Timestamp time_since_close_started_;
  bool sc_request_running_{false};
  td::uint64 sc_request_qid_{0};

  double last_request_at_{0};
};
}  // namespace cocoon
