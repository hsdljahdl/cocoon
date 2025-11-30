#pragma once

#include "SmartContract.hpp"
#include "RootContractConfig.hpp"
#include "block.h"
#include "common/bitstring.h"
#include "crypto/Ed25519.h"
#include "td/actor/PromiseFuture.h"
#include "td/utils/Time.h"
#include "td/utils/common.h"
#include "vm/cells/Cell.h"
#include "vm/cells/CellSlice.h"
#include <memory>

namespace cocoon {

class WorkerContract : public TonScWrapper {
 public:
  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void on_transaction(const block::StdAddress &src_address, td::uint32 op, td::uint64 qid) {
    }
  };
  WorkerContract(const block::StdAddress &owner_address, const block::StdAddress &proxy_sc_address,
                 const td::Bits256 &proxy_public_key, std::unique_ptr<Callback> callback, BaseRunner *root,
                 std::shared_ptr<RunnerConfig> runner_config);
  WorkerContract(const block::StdAddress &owner_address, const block::StdAddress &proxy_sc_address,
                 const td::Bits256 &proxy_public_key, BaseRunner *root, std::shared_ptr<RunnerConfig> runner_config)
      : WorkerContract(owner_address, proxy_sc_address, proxy_public_key, nullptr, root, std::move(runner_config)) {
  }

  void set_callback(std::unique_ptr<Callback> callback) {
    callback_ = std::move(callback);
  }

  td::Ref<vm::Cell> init_data_cell() override;

  void on_state_update(ton::tl_object_ptr<ton::tonlib_api::raw_fullAccountState> state) override;
  void on_transaction(ton::tl_object_ptr<ton::tonlib_api::raw_transaction> state) override;

  void alarm() override {
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

  td::Ref<vm::Cell> create_pay_message(td::int64 seqno);
  td::Ref<vm::Cell> create_last_pay_message(td::int64 seqno);

  td::Result<td::int64> check_signed_pay_message(vm::CellSlice &cs);
  td::Result<td::int64> check_signed_pay_message(td::Slice data);

  td::Ref<vm::Cell> repack_signed_pay_message(td::Slice data, const block::StdAddress &send_excesses_to,
                                              td::uint64 *qid);

  auto tokens_processed() const {
    return tokens_processed_;
  }

  void init_pseudo_state() override {
    tokens_processed_ = 0;
  }

  auto state() const {
    return state_;
  }

 private:
  const block::StdAddress owner_address_;
  const block::StdAddress proxy_sc_address_;
  const td::Bits256 proxy_public_key_;

  td::uint32 state_{3};
  td::int64 tokens_processed_{0};

  std::unique_ptr<Callback> callback_;
};

}  // namespace cocoon
