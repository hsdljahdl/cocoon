#include "ClientContract.hpp"
#include "runners/BaseRunner.hpp"
#include "auto/tl/tonlib_api.h"
#include "Opcodes.hpp"
#include "block.h"
#include "checksum.h"
#include "cocoon-tl-utils/parsers.hpp"
#include "common/bitstring.h"
#include "td/utils/Random.h"
#include "td/utils/format.h"
#include "vm/cells/CellBuilder.h"
#include "vm/cells/CellSlice.h"
#include <memory>

namespace cocoon {

ClientContract::ClientContract(const block::StdAddress &owner_address, const block::StdAddress &proxy_sc_address,
                               td::Bits256 proxy_public_key, std::unique_ptr<Callback> callback, BaseRunner *runner,
                               std::shared_ptr<RunnerConfig> runner_config)
    : TonScWrapper({}, {}, runner, std::move(runner_config))
    , owner_address_(owner_address)
    , proxy_sc_address_(proxy_sc_address)
    , proxy_public_key_(proxy_public_key)
    , callback_(std::move(callback)) {
  set_code(this->runner_config()->root_contract_config->client_sc_code());
  sc_update_address();
}

td::int64 ClientContract::deploy_balance() const {
  return to_nano(0.8);
}

void ClientContract::on_state_update(ton::tl_object_ptr<ton::tonlib_api::raw_fullAccountState> raw_state) {
  if (raw_state->data_.size() == 0) {
    state_ = 0;
    balance_ = 0;
    tokens_used_ = 0;
    unlock_ts_ = 0;
    secret_hash_ = td::Bits256::zero();
    return;
  }
  auto R = vm::std_boc_deserialize(raw_state->data_);
  if (R.is_error()) {
    LOG(FATAL) << "failed to deserialize client contract state: " << R.move_as_error();
    return;
  }

  vm::CellSlice cs{vm::NoVm{}, R.move_as_ok()};
  unsigned long long state = 0;
  td::uint64 balance = 0;
  td::uint64 stake = 0;
  long long tokens_used = 0, unlock_ts = 0;
  td::Bits256 secret_hash;
  if (!cs.fetch_ulong_bool(2, state) || !fetch_coins(cs, balance) || !fetch_coins(cs, stake) ||
      !cs.fetch_long_bool(64, tokens_used) || !cs.fetch_long_bool(32, unlock_ts) ||
      !cs.fetch_bytes(secret_hash.as_slice())) {
    LOG(FATAL) << "failed to parse client contract state: not enough data";
    return;
  }
  state_ = (td::int32)state;
  balance_ = balance;
  stake_ = stake;
  tokens_used_ = tokens_used;
  unlock_ts_ = (td::int32)unlock_ts;
  secret_hash_ = secret_hash;
}

void ClientContract::on_transaction(ton::tl_object_ptr<ton::tonlib_api::raw_transaction> trans) {
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

  if (!cs.fetch_ulong_bool(32, op) || !cs.fetch_ulong_bool(64, qid)) {
    return;
  }

  if (callback_) {
    callback_->on_transaction(source, (td::uint32)op, qid);
  }
}

td::Ref<vm::Cell> ClientContract::create_proxy_register_message(td::uint64 nonce, td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::owner_client_register, 32)
      .store_long(qid ?: td::Random::fast_uint64(), 64)
      .store_long(nonce, 64);
  store_address(cb, runner()->cocoon_wallet()->address());  // send_excesses_to
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_change_secret_hash_message(const td::Bits256 &secret_hash, td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::owner_client_change_secret_hash, 32)
      .store_long(qid ?: td::Random::fast_uint64(), 64)
      .store_bytes(secret_hash.as_slice());
  store_address(cb, runner()->cocoon_wallet()->address());  // send_excesses_to
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_topup_message(td::int64 coins, td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::ext_client_top_up, 32).store_long(qid ?: td::Random::fast_uint64(), 64);
  store_coins(cb, coins);
  store_address(cb, runner()->cocoon_wallet()->address());  // send_excesses_to
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_topup_and_reopen_message(td::int64 coins, td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::owner_client_top_up_reopen, 32).store_long(qid ?: td::Random::fast_uint64(), 64);
  store_coins(cb, coins);
  store_address(cb, runner()->cocoon_wallet()->address());  // send_excesses_to
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_charge_message(td::int64 tokens, td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::ext_client_charge_signed, 32)
      .store_long(qid ?: td::Random::fast_uint64(), 64)
      .store_long(tokens, 64);
  store_address(cb, address());
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_refund_message(td::int64 tokens, td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::ext_client_grant_refund_signed, 32)
      .store_long(qid ?: td::Random::fast_uint64(), 64)
      .store_long(tokens, 64);
  store_address(cb, address());
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_withdraw_message(td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::owner_client_withdraw, 32).store_long(qid ?: td::Random::fast_uint64(), 64);
  store_address(cb, runner()->cocoon_wallet()->address());  // send_excesses_to
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_request_refund_message(td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::owner_client_request_refund, 32).store_long(qid ?: td::Random::fast_uint64(), 64);
  store_address(cb, runner()->cocoon_wallet()->address());  // send_excesses_to
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::create_increase_stake_message(td::int64 new_stake, td::uint64 qid) {
  vm::CellBuilder cb;
  cb.store_long(opcodes::owner_client_increase_stake, 32).store_long(qid ?: td::Random::fast_uint64(), 64);
  store_address(cb, runner()->cocoon_wallet()->address());  // send_excesses_to
  return cb.finalize();
}

td::Ref<vm::Cell> ClientContract::init_data_cell() {
  const auto &root_contract_config = runner_config()->root_contract_config;
  vm::CellBuilder cb_cd;
  store_address(cb_cd, owner_address_);
  store_address(cb_cd, proxy_sc_address_);
  cb_cd.store_bytes(proxy_public_key_.as_slice());
  vm::CellBuilder cb;
  cb.store_long(0, 2);
  store_coins(cb, 0);
  store_coins(cb, root_contract_config->min_client_stake());  // stake
  cb.store_long(0, 64).store_long(0, 32).store_zeroes(256).store_ref(cb_cd.finalize());
  cb.store_ref(root_contract_config->serialize_client_params_cell());
  return cb.finalize();
}

td::Result<td::int64> ClientContract::check_signed_pay_message(vm::CellSlice &cs) {
  unsigned long long op, op_copy, qid, qid_copy, tokens_processed;
  block::StdAddress addr;
  td::uint8 signature[64];
  if (!cs.fetch_ulong_bool(32, op) || op != opcodes::ext_client_charge_signed || !cs.fetch_ulong_bool(64, qid) ||
      !fetch_address(cs, addr, runner_config()->is_testnet, false) || !cs.fetch_bytes(signature, 64) ||
      cs.size() != 0) {
    return td::Status::Error("failed to parse");
  }

  if (cs.size_refs() != 1) {
    return td::Status::Error("failed to parse");
  }

  auto payload = cs.fetch_ref();

  auto hash = payload->get_hash();

  vm::CellSlice pcs(vm::NoVm{}, payload);

  if (!pcs.fetch_ulong_bool(32, op_copy) || op_copy != op || !pcs.fetch_ulong_bool(64, qid_copy) || qid_copy != qid ||
      !pcs.fetch_ulong_bool(64, tokens_processed) || !fetch_address(pcs, addr, runner_config()->is_testnet, false) ||
      pcs.size() != 0) {
    return td::Status::Error("failed to parse");
  }

  if (addr.addr != address().addr || addr.workchain != address().workchain) {
    return td::Status::Error("bad owner");
  }

  td::Ed25519::PublicKey pubk(td::SecureString(proxy_public_key_.as_slice()));
  TRY_STATUS(pubk.verify_signature(hash.as_slice(), td::Slice((char *)signature, 64)));

  return (td::int64)tokens_processed;
}

td::Result<td::int64> ClientContract::check_signed_pay_message(td::Slice data) {
  auto R = vm::std_boc_deserialize(data);
  if (R.is_error()) {
    return R.move_as_error();
  }
  vm::CellSlice cs(vm::NoVm{}, R.move_as_ok());
  return check_signed_pay_message(cs);
}

td::Ref<vm::Cell> ClientContract::repack_signed_pay_message(td::Slice data, const block::StdAddress &send_excesses_to,
                                                            td::uint64 *qid_ptr) {
  auto R = vm::std_boc_deserialize(data).move_as_ok();
  vm::CellSlice cs(vm::NoVm{}, std::move(R));

  unsigned long long op, qid;
  block::StdAddress addr;
  td::uint8 signature[64];
  CHECK(cs.fetch_ulong_bool(32, op) && op == opcodes::ext_client_charge_signed && cs.fetch_ulong_bool(64, qid) &&
        fetch_address(cs, addr, runner_config()->is_testnet, false) && cs.fetch_bytes(signature, 64) && cs.size() == 0);

  vm::CellBuilder cb;
  cb.store_long(op, 32).store_long(qid, 64);
  store_address(cb, send_excesses_to);
  cb.store_bytes(signature, 64);
  cb.store_ref(cs.fetch_ref());

  if (qid_ptr) {
    *qid_ptr = qid;
  }

  return cb.finalize();
}

}  // namespace cocoon
