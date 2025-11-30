#include "ProxyContract.hpp"
#include "auto/tl/tonlib_api.h"
#include "Opcodes.hpp"
#include "block.h"
#include "cocoon-tl-utils/parsers.hpp"
#include "common/bitstring.h"
#include "td/utils/Random.h"
#include "td/utils/format.h"
#include "vm/cells/Cell.h"
#include <memory>

namespace cocoon {

ProxyContract::ProxyContract(const block::StdAddress &owner_address, const td::Bits256 &public_key,
                             std::unique_ptr<Callback> callback, BaseRunner *runner,
                             std::shared_ptr<RunnerConfig> runner_config)
    : TonScWrapper({}, {}, runner, std::move(runner_config))
    , owner_address_(owner_address)
    , public_key_(public_key)
    , callback_(std::move(callback)) {
  set_code(this->runner_config()->root_contract_config->proxy_sc_code());
  sc_update_address();
}

td::Ref<vm::Cell> ProxyContract::init_data_cell() {
  const auto &root_contract_config = runner_config()->root_contract_config;
  vm::CellBuilder cb;
  store_address(cb, owner_address_);                                  // owner_address
  cb.store_bytes(public_key_.as_slice());                             // public key
  store_address(cb, runner()->root_contract_address());               // root_address
  cb.store_long(0, 2);                                                // state
  store_coins(cb, 0);                                                 // balance
  store_coins(cb, 0);                                                 // stake
  cb.store_long(0, 32);                                               // unlock_ts
  cb.store_ref(root_contract_config->serialize_proxy_params_cell());  // params
  return cb.finalize();
}

void ProxyContract::on_transaction(ton::tl_object_ptr<ton::tonlib_api::raw_transaction> trans) {
  auto &in_msg = trans->in_msg_;
  CHECK(in_msg);

  if (in_msg->msg_data_->get_id() != ton::tonlib_api::msg_dataRaw::ID) {
    LOG(FATAL) << "msg data not in raw format";
    return;
  }

  auto data = static_cast<ton::tonlib_api::msg_dataRaw *>(in_msg->msg_data_.get())->body_;

  auto R = vm::std_boc_deserialize(data);
  if (R.is_error()) {
    LOG(ERROR) << "failed to deserialize inbound message: " << R.move_as_error();
    return;
  }

  block::StdAddress source;
  if (!rdeserialize(source, trans->in_msg_->source_->account_address_, runner_config()->is_testnet)) {
    LOG(ERROR) << "failed to deserialize inbound message source: " << trans->in_msg_->source_->account_address_;
    return;
  }

  vm::CellSlice cs{vm::NoVm{}, R.move_as_ok()};

  unsigned long long op = 0;
  unsigned long long qid = 0;
  block::StdAddress src_owner_address;

  if (!cs.fetch_ulong_bool(32, op) || !cs.fetch_ulong_bool(64, qid)) {
    return;
  }

  LOG(DEBUG) << "proxy contract: received message with type " << td::format::as_hex(op);

  if (op == opcodes::client_proxy_request) {
    if (!fetch_address(cs, src_owner_address, runner_config()->is_testnet, false)) {
      LOG(INFO) << "cannot fetch address client owner address";
      return;
    }
    auto expected_contract_address = runner()->generate_client_sc_address(public_key_, owner_address_, address(),
                                                                          src_owner_address, runner_config());
    if (expected_contract_address.workchain != source.workchain || expected_contract_address.addr != source.addr) {
      LOG(INFO) << "got client message from non-client";
      return;
    }

    td::uint32 state;
    td::uint64 new_balance, new_stake, tokens_used;
    td::Bits256 secret_hash;

    auto state_ref = cs.fetch_ref();

    vm::CellSlice scs{vm::NoVm{}, state_ref};
    CHECK(scs.fetch_uint_to(2, state));
    CHECK(fetch_coins(scs, new_balance));
    CHECK(fetch_coins(scs, new_stake));
    CHECK(scs.fetch_uint_to(64, tokens_used));
    CHECK(scs.fetch_bytes(secret_hash.as_slice()));
    CHECK(scs.empty_ext());

    callback_->on_client_update(src_owner_address, expected_contract_address, state, new_balance, new_stake,
                                tokens_used, secret_hash);

    bool has_payload;
    CHECK(cs.fetch_bool_to(has_payload));

    if (!has_payload) {
      CHECK(cs.empty_ext());
      return;
    }

    auto payload_cell = cs.fetch_ref();
    CHECK(cs.empty_ext());
    vm::CellSlice pcs{vm::NoVm{}, payload_cell};

    td::uint32 in_op;
    CHECK(pcs.fetch_uint_to(32, in_op));

    LOG(DEBUG) << "proxy contract: received client message with subtype " << td::format::as_hex(in_op);

    switch (in_op) {
      case opcodes::client_proxy_top_up:
        return;
      case opcodes::client_proxy_register: {
        td::uint64 nonce;
        CHECK(pcs.fetch_uint_to(64, nonce));
        callback_->on_client_register(src_owner_address, expected_contract_address, nonce);
        return;
      }
      case opcodes::client_proxy_refund_granted:
        return;
      case opcodes::client_proxy_refund_force:
        return;
      default:
        LOG(ERROR) << "proxy contract: received client message with unknown subtype " << td::format::as_hex(in_op);
        return;
    }
  } else if (op == opcodes::worker_proxy_request) {
    if (!fetch_address(cs, src_owner_address, runner_config()->is_testnet, false)) {
      LOG(INFO) << "cannot fetch address worker owner address";
      return;
    }
    auto expected_contract_address = runner()->generate_worker_sc_address(public_key_, owner_address_, address(),
                                                                          src_owner_address, runner_config());
    if (expected_contract_address.workchain != source.workchain || expected_contract_address.addr != source.addr) {
      LOG(INFO) << "got worker message from non-worker";
      return;
    }

    td::uint32 state;
    td::uint64 tokens_used;
    CHECK(cs.fetch_uint_to(2, state));
    CHECK(cs.fetch_uint_to(64, tokens_used));

    callback_->on_worker_update(src_owner_address, expected_contract_address, state, tokens_used);

    bool has_payload;
    CHECK(cs.fetch_bool_to(has_payload));

    if (!has_payload) {
      CHECK(cs.empty_ext());
      return;
    }

    auto payload_cell = cs.fetch_ref();
    CHECK(cs.empty_ext());
    vm::CellSlice pcs{vm::NoVm{}, payload_cell};

    td::uint32 in_op;
    CHECK(pcs.fetch_uint_to(32, in_op));

    LOG(DEBUG) << "proxy contract: received worker message with subtype " << td::format::as_hex(in_op);

    switch (in_op) {
      case opcodes::worker_proxy_payout_request: {
        td::uint64 tokens;
        CHECK(pcs.fetch_uint_to(64, tokens));
        callback_->on_worker_payout(src_owner_address, expected_contract_address, tokens);
        return;
      }
      default:
        LOG(ERROR) << "proxy contract: received worker message with unknown subtype " << td::format::as_hex(in_op);
        return;
    }
  } else if (op == opcodes::do_not_process) {
    auto w = runner()->cocoon_wallet();
    if (!w) {
      return;
    }
    if (source.workchain != w->address().workchain || source.addr != w->address().addr) {
      return;
    }

    td::uint32 in_op;
    if (!cs.fetch_uint_to(32, in_op)) {
      LOG(ERROR) << "proxy contract: received incorrect message from our wallet";
      return;
    }

    if (in_op == opcodes::proxy_save_state) {
      td::int32 seqno;
      td::Bits256 unique_hash;
      if (!cs.fetch_int_to(32, seqno) || !cs.fetch_bytes(unique_hash.as_slice()) || !cs.empty_ext()) {
        LOG(ERROR) << "proxy contract: received incorrect message from our wallet: incorrect save_state message";
        return;
      }

      callback_->proxy_save_state(seqno, unique_hash);
      return;
    }
  }
}

void ProxyContract::on_state_update(ton::tl_object_ptr<ton::tonlib_api::raw_fullAccountState> state) {
  if (state->data_.size() == 0) {
    return;
  }
  auto R = vm::std_boc_deserialize(state->data_);
  R.ensure();

  vm::CellSlice cs{vm::NoVm{}, R.move_as_ok()};

  unsigned long long status = 0, unlock_ts = 0;
  td::uint64 balance = 0, stake = 0;
  block::StdAddress tmp;
  td::Ref<vm::Cell> tmp_ref;

  CHECK(fetch_address(cs, tmp, runner_config()->is_testnet, false));  // owner
  CHECK(cs.skip_first(256));                                          // public key
  CHECK(fetch_address(cs, tmp, runner_config()->is_testnet, false));  // root address
  CHECK(cs.fetch_ulong_bool(2, status));                              // status
  CHECK(fetch_coins(cs, balance));                                    // balance
  CHECK(fetch_coins(cs, stake));                                      // balance
  CHECK(cs.fetch_ulong_bool(32, unlock_ts));                          // unlock_ts
  CHECK(cs.fetch_ref_to(tmp_ref));                                    // params

  status_ = (td::uint32)status;
  unlock_ts_ = (td::int32)unlock_ts;
  ready_for_withdraw_ = balance;
  stake_ = stake;
}

td::Ref<vm::Cell> ProxyContract::create_start_close_message() {
  vm::CellBuilder cb;
  cb.store_long(opcodes::owner_proxy_close, 32);
  store_address(cb, runner()->cocoon_wallet_address());
  return cb.finalize();
}

td::Ref<vm::Cell> ProxyContract::create_withdraw_message() {
  vm::CellBuilder cb;
  cb.store_long(opcodes::ext_proxy_payout_request, 32).store_long(td::Random::fast_uint64(), 64);
  store_address(cb, runner()->cocoon_wallet_address());
  return cb.finalize();
}

td::Ref<vm::Cell> ProxyContract::create_save_state_message(td::int32 seqno, const td::Bits256 &unique_hash) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::do_not_process, 32)
      .store_long(td::Random::fast_uint64(), 64)
      .store_long(opcodes::proxy_save_state, 32)
      .store_long(seqno, 32)
      .store_bytes(unique_hash.as_slice());
  return cb.finalize();
}

}  // namespace cocoon
