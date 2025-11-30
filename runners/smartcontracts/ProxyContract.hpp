#pragma once

#include "SmartContract.hpp"
#include "RootContractConfig.hpp"
#include "block.h"
#include "common/bitstring.h"
#include "runners/BaseRunner.hpp"
#include "crypto/Ed25519.h"
#include "td/actor/PromiseFuture.h"
#include "td/db/KeyValueAsync.h"
#include "td/utils/Time.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "vm/cells/Cell.h"
#include <memory>

namespace cocoon {

class ProxyRunner;

class ProxyContract : public TonScWrapper {
 public:
  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void on_deploy() {
    }
    virtual void on_client_update(const block::StdAddress &client_owner_address,
                                  const block::StdAddress &client_sc_address, td::uint32 state, td::int64 new_balance,
                                  td::int64 new_stake, td::int64 tokens_used, const td::Bits256 &secret_hash) {
    }
    virtual void on_client_register(const block::StdAddress &client_owner_address,
                                    const block::StdAddress &client_sc_address, td::uint64 nonce) {
    }
    virtual void on_worker_update(const block::StdAddress &client_owner_address,
                                  const block::StdAddress &client_sc_address, td::uint32 state, td::int64 tokens) {
    }
    virtual void on_worker_payout(const block::StdAddress &worker_owner_address,
                                  const block::StdAddress &worker_sc_address, td::int64 tokens_delta) {
    }
    virtual void proxy_save_state(td::int32 seqno, const td::Bits256 &unique_hash) {
    }
  };

  ProxyContract(const block::StdAddress &owner_address, const td::Bits256 &public_key,
                std::unique_ptr<Callback> callback, BaseRunner *runner, std::shared_ptr<RunnerConfig> runner_config);
  td::Ref<vm::Cell> init_data_cell() override;
  td::Ref<vm::Cell> create_start_close_message();
  td::Ref<vm::Cell> create_withdraw_message();
  td::Ref<vm::Cell> create_save_state_message(td::int32 seqno, const td::Bits256 &unique_hash);

  void on_state_update(ton::tl_object_ptr<ton::tonlib_api::raw_fullAccountState> state) override;
  void on_transaction(ton::tl_object_ptr<ton::tonlib_api::raw_transaction> state) override;

  void alarm() override {
  }

  bool code_is_inited() const {
    return balance() >= 0;
  }

  void init_pseudo_state() override {
    status_ = 0;
    ready_for_withdraw_ = 10000000;
  }

  auto price_per_token() const {
    return runner_config()->root_contract_config->price_per_token();
  }

  auto worker_fee_per_token() const {
    return runner_config()->root_contract_config->worker_fee_per_token();
  }

  auto ready_for_withdraw() const {
    return ready_for_withdraw_;
  }

  auto stake() const {
    return stake_;
  }

 private:
  block::StdAddress owner_address_;
  td::Bits256 public_key_;

  std::unique_ptr<Callback> callback_;

  td::uint32 status_{3};
  td::int32 unlock_ts_ = 0;
  td::int64 ready_for_withdraw_ = 0;
  td::int64 stake_ = 0;
};

}  // namespace cocoon
