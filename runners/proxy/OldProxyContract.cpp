#include "OldProxyContract.hpp"
#include "ProxyRunner.hpp"
#include "runners/smartcontracts/Opcodes.hpp"
#include "runners/smartcontracts/ClientContract.hpp"
#include "runners/smartcontracts/WorkerContract.hpp"

#include "cocoon-tl-utils/cocoon-tl-utils.hpp"
#include "cocoon-tl-utils/parsers.hpp"

namespace cocoon {

OldProxyContract::OldProxyContract(const cocoon_api::proxyDb_oldInstance &instance, ProxyRunner *proxy_runner)
    : closing_state_(instance.closing_state_)
    , close_at_(instance.close_at_)
    , next_client_(instance.next_client_)
    , next_worker_(instance.next_worker_)
    , proxy_runner_(proxy_runner) {
  CHECK(proxy_runner_->rdeserialize(sc_addr_, instance.contract_address_));
  sc_addr_.testnet = proxy_runner_->is_testnet();

  next_client_ = instance.next_client_;
  next_worker_ = instance.next_worker_;

  auto conf =
      RootContractConfig::load_from_tl(*instance.root_contract_state_, proxy_runner_->is_testnet()).move_as_ok();

  config_ = std::make_shared<RunnerConfig>();
  config_->is_testnet = proxy_runner_->is_testnet();
  config_->ton_disabled = false;
  config_->root_contract_ts = 0;
  config_->root_contract_config = std::move(conf);
}

void OldProxyContract::message_sent_success() {
  CHECK(running_message_);
  running_message_ = false;
  if (closing_state_ == ClosingState::NotStarted) {
    closing_state_ = ClosingState::StartedClients;
    close_at_ = (td::int32)((int)std::time(0) + config_->root_contract_config->proxy_delay_before_close() + 1);
    advance_state();
    proxy_runner_->set_to_db(PSTRING() << "oldproxycontract_" << sc_addr_.rserialize(true),
                             cocoon::serialize_tl_object(serialize(), true).as_slice());
    return;
  } else if (closing_state_ == ClosingState::StartedClients) {
    auto B = proxy_runner_->get_from_db(next_client_);
    CHECK(B.size() > 0);
    auto obj = cocoon::fetch_tl_object<cocoon_api::proxyDb_oldClient>(std::move(B), true).move_as_ok();
    auto cur_client = next_client_;
    next_client_ = obj->next_client_;
    advance_state();

    proxy_runner_->db_transaction([&]() {
      proxy_runner_->del_from_db(cur_client);
      proxy_runner_->set_to_db(PSTRING() << "oldproxycontract_" << sc_addr_.rserialize(true),
                               cocoon::serialize_tl_object(serialize(), true).as_slice());
    });

    return;
  } else if (closing_state_ == ClosingState::StartedWorkers) {
    auto B = proxy_runner_->get_from_db(next_worker_);
    CHECK(B.size() > 0);
    auto obj = cocoon::fetch_tl_object<cocoon_api::proxyDb_oldWorker>(std::move(B), true).move_as_ok();
    auto cur_worker = next_worker_;
    next_worker_ = obj->next_worker_;
    advance_state();

    proxy_runner_->db_transaction([&]() {
      proxy_runner_->del_from_db(cur_worker);
      proxy_runner_->set_to_db(PSTRING() << "oldproxycontract_" << sc_addr_.rserialize(true),
                               cocoon::serialize_tl_object(serialize(), true).as_slice());
    });

    return;
  } else if (closing_state_ == ClosingState::Closing) {
    closing_state_ = ClosingState::Closed;
    proxy_runner_->del_from_db(PSTRING() << "oldproxycontract_" << sc_addr_.rserialize(true));
    return;
  } else {
    UNREACHABLE();
  }
}

void OldProxyContract::send_next_message() {
  auto P = td::PromiseCreator::lambda([self = this](td::Result<td::Unit> R) {
    R.ensure();
    self->message_sent_success();
  });

  td::Ref<vm::Cell> signed_msg;
  if (closing_state_ == ClosingState::NotStarted) {
    vm::CellBuilder cb;
    cb.store_long(opcodes::ext_proxy_close_request_signed, 32);
    cb.store_long(td::Random::fast_uint64(), 64);
    store_address(cb, sc_addr_);

    auto msg = cb.finalize();
    auto signed_msg = proxy_runner_->sign_and_wrap_message(msg, proxy_runner_->cocoon_wallet()->address());
    CHECK(signed_msg.not_null());

    running_message_ = true;
    proxy_runner_->cocoon_wallet()->send_transaction(sc_addr_, to_nano(1), {}, std::move(signed_msg), std::move(P));
  } else if (closing_state_ == ClosingState::StartedClients) {
    auto B = proxy_runner_->get_from_db(next_client_);
    CHECK(B.size() > 0);
    auto obj = cocoon::fetch_tl_object<cocoon_api::proxyDb_oldClient>(std::move(B), true).move_as_ok();
    block::StdAddress addr;
    CHECK(proxy_runner_->rdeserialize(addr, obj->owner_address_));
    ClientContract sc(addr, sc_addr_, proxy_runner_->public_key(), proxy_runner_, config_);
    auto msg = sc.create_refund_message(obj->tokens_);
    auto signed_msg = proxy_runner_->sign_and_wrap_message(msg, proxy_runner_->cocoon_wallet()->address());
    CHECK(signed_msg.not_null());

    running_message_ = true;
    proxy_runner_->cocoon_wallet()->send_transaction(sc.address(), to_nano(1), {}, std::move(signed_msg), std::move(P));
  } else if (closing_state_ == ClosingState::StartedWorkers) {
    auto B = proxy_runner_->get_from_db(next_worker_);
    CHECK(B.size() > 0);
    auto obj = cocoon::fetch_tl_object<cocoon_api::proxyDb_oldWorker>(std::move(B), true).move_as_ok();
    block::StdAddress addr;
    CHECK(proxy_runner_->rdeserialize(addr, obj->owner_address_));
    WorkerContract sc(addr, sc_addr_, proxy_runner_->public_key(), proxy_runner_, config_);
    auto msg = sc.create_last_pay_message(obj->tokens_);
    auto signed_msg = proxy_runner_->sign_and_wrap_message(msg, proxy_runner_->cocoon_wallet()->address());
    CHECK(signed_msg.not_null());

    running_message_ = true;
    proxy_runner_->cocoon_wallet()->send_transaction(sc.address(), to_nano(1), {}, std::move(signed_msg), std::move(P));
  } else if (closing_state_ == ClosingState::Closing) {
    vm::CellBuilder cb;
    cb.store_long(opcodes::ext_proxy_close_complete_request_signed, 32);
    cb.store_long(td::Random::fast_uint64(), 64);
    store_address(cb, sc_addr_);

    auto msg = cb.finalize();
    auto signed_msg = proxy_runner_->sign_and_wrap_message(msg, proxy_runner_->cocoon_wallet()->address());
    CHECK(signed_msg.not_null());

    running_message_ = true;
    proxy_runner_->cocoon_wallet()->send_transaction(sc_addr_, to_nano(1), {}, std::move(signed_msg), std::move(P));
  } else {
    UNREACHABLE();
  }
}

ClientCheckResult OldProxyContract::check() {
  if (is_finished()) {
    return ClientCheckResult::Delete;
  }
  if (ready_to_send_next_message()) {
    send_next_message();
  }
  return ClientCheckResult::Ok;
}

void OldProxyContract::store_stats(td::StringBuilder &sb) {
  sb << "<table>\n";
  sb << "<tr><td>sc address</td><td>" << proxy_runner_->address_link(sc_addr_) << "</td></tr>\n";
  sb << "<tr><td>state</td><td>" << state() << "</td></tr>\n";
  sb << "<tr><td>running message</td><td>" << (running_message() ? "YES" : "NO") << "</td></tr>\n";
  sb << "<tr><td>close at</td><td>" << close_at_ << "</td></tr>\n";
  sb << "</table>\n";
}

void OldProxyContract::store_stats(SimpleJsonSerializer &jb) {
  jb.start_object();
  jb.add_element("sc_address", sc_addr_.rserialize(true));
  jb.add_element("state", state());
  jb.add_element("running_message", running_message());
  jb.add_element("close_at", close_at_);
  jb.stop_object();
}

}  // namespace cocoon
