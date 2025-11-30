#pragma once

#include "SmartContract.hpp"
#include "RootContractConfig.hpp"
#include "block.h"
#include "checksum.h"
#include "common/bitstring.h"
#include "crypto/Ed25519.h"
#include "td/actor/PromiseFuture.h"
#include "td/utils/Time.h"
#include "td/utils/common.h"
#include "vm/cells/Cell.h"
#include "runners/helpers/Ton.h"
#include <memory>

namespace cocoon {

class ClientContract : public TonScWrapper {
 public:
  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void on_transaction(const block::StdAddress &src_address, td::uint32 op, td::uint64 qid) {
    }
  };
  ClientContract(const block::StdAddress &owner_address, const block::StdAddress &proxy_sc_address,
                 td::Bits256 proxy_public_key, std::unique_ptr<Callback> callback, BaseRunner *runner,
                 std::shared_ptr<RunnerConfig> runner_config);
  ClientContract(const block::StdAddress &owner_address, const block::StdAddress &proxy_sc_address,
                 td::Bits256 proxy_public_key, BaseRunner *runner, std::shared_ptr<RunnerConfig> runner_config)
      : ClientContract(owner_address, proxy_sc_address, proxy_public_key, nullptr, runner, std::move(runner_config)) {
  }

  td::Ref<vm::Cell> init_data_cell() override;
  td::int64 deploy_balance() const override;

  void on_state_update(ton::tl_object_ptr<ton::tonlib_api::raw_fullAccountState> state) override;
  void on_transaction(ton::tl_object_ptr<ton::tonlib_api::raw_transaction> state) override;

  void set_callback(std::unique_ptr<Callback> callback) {
    callback_ = std::move(callback);
  }

  td::Ref<vm::Cell> create_proxy_register_message(td::uint64 nonce, td::uint64 qid = 0);
  td::Ref<vm::Cell> create_change_secret_hash_message(const td::Bits256 &secret_hash, td::uint64 qid = 0);
  td::Ref<vm::Cell> create_topup_message(td::int64 coins, td::uint64 qid = 0);
  td::Ref<vm::Cell> create_topup_and_reopen_message(td::int64 coins, td::uint64 qid = 0);
  td::Ref<vm::Cell> create_charge_message(td::int64 tokens, td::uint64 qid = 0);
  td::Ref<vm::Cell> create_refund_message(td::int64 tokens, td::uint64 qid = 0);
  td::Ref<vm::Cell> create_request_refund_message(td::uint64 qid = 0);
  td::Ref<vm::Cell> create_withdraw_message(td::uint64 qid = 0);
  td::Ref<vm::Cell> create_increase_stake_message(td::int64 new_stake, td::uint64 qid = 0);

  bool allow_queries() const {
    return state_ == 0;
  }

  const auto &secret_hash() const {
    return secret_hash_;
  }

  const auto &owner_address() const {
    return owner_address_;
  }

  const auto &proxy_sc_address() const {
    return proxy_sc_address_;
  }

  const auto &proxy_public_key() const {
    return proxy_public_key_;
  }

  void init_pseudo_state() override {
    state_ = 0;
    balance_ = to_nano(1e6);
    stake_ = to_nano(1e6);
    tokens_used_ = 0;
    unlock_ts_ = 0;
    secret_hash_ = td::sha256_bits256("");
  }

  td::uint64 balance_for_requests() const {
    return balance_;
  }

  auto stake() const {
    return stake_;
  }

  auto tokens_used() const {
    return tokens_used_;
  }

  auto unlock_ts() const {
    return unlock_ts_;
  }

  auto state() const {
    return state_;
  }

  td::Result<td::int64> check_signed_pay_message(vm::CellSlice &cs);
  td::Result<td::int64> check_signed_pay_message(td::Slice data);

  td::Ref<vm::Cell> repack_signed_pay_message(td::Slice data, const block::StdAddress &send_excesses_to,
                                              td::uint64 *qid);

  static std::string state_to_string(td::int32 state) {
    switch (state) {
      case 0:
        return "running";
      case 1:
        return "closing";
      case 2:
        return "closed";
      case 3:
      default:
        return "unknown";
    }
  }

  std::string state_as_string() const {
    return state_to_string(state_);
  }

 private:
  const block::StdAddress owner_address_;
  const block::StdAddress proxy_sc_address_;
  const td::Bits256 proxy_public_key_;

  td::int32 state_{-1};
  td::uint64 balance_{0};
  td::uint64 stake_{0};
  td::uint64 tokens_used_{0};
  td::uint32 unlock_ts_{0};
  td::Bits256 secret_hash_ = td::Bits256::zero();

  std::unique_ptr<Callback> callback_;
};

}  // namespace cocoon
