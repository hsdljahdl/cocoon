#include "ClientProxyInfo.h"
#include "ClientRunner.hpp"
#include "auto/tl/cocoon_api.h"
#include "td/actor/actor.h"
#include "td/utils/port/Clocks.h"
#include <memory>

namespace cocoon {

ClientProxyInfo::ClientProxyInfo(ClientRunner *runner, td::Bits256 proxy_public_key,
                                 block::StdAddress proxy_sc_address) {
  last_request_at_monotonic_ = td::Clocks::monotonic();

  class Callback : public ClientContract::Callback {
   public:
    Callback(ClientProxyInfo *self) : self_(self) {
    }
    void on_transaction(const block::StdAddress &src_address, td::uint32 op, td::uint64 qid) override {
      self_->sc_request_completed(src_address, op, qid);
    }

   private:
    ClientProxyInfo *self_;
  };

  sc_ = std::make_shared<ClientContract>(runner->cocoon_wallet()->address(), proxy_sc_address, proxy_public_key,
                                         std::make_unique<Callback>(this), runner, runner->runner_config());
  sc_->subscribe_to_updates(sc_);
  sc_->deploy([](td::Result<td::Unit> R) { R.ensure(); });
}

ClientCheckResult ClientProxyInfo::check() {
  update_from_sc();

  if (sc_request_running() || !exp_sc_is_inited() || sc_->runner_config()->ton_disabled) {
    return ClientCheckResult::Ok;
  }

  if (exp_sc_is_closed()) {
    return ClientCheckResult::Delete;
  }

  ClientRunner *runner = static_cast<ClientRunner *>(sc_->runner());

  auto actual_params_version = runner->runner_config()->root_contract_config->params_version();

  /* need close this contract and re-create a new one with new parameters */
  if (sc_->runner_config()->root_contract_config->params_version() < actual_params_version) {
    if (exp_sc_is_closing()) {
      auto ts = exp_sc_unlock_ts_;
      if (ts > 0 && ts < (int)std::time(0)) {
        LOG(INFO) << "sending close(II) for proxy " << proxy_sc_address().rserialize(true);
        auto msg = run_close();
        sc_->runner()->cocoon_wallet()->send_transaction(sc_->address(), to_nano(0.7), {}, std::move(msg),
                                                         runner->create_proxy_sc_request_promise(shared_ptr()));
      }
      return ClientCheckResult::Ok;
    } else /*sc_state == 0 */ {
      auto waiting_for = outdated_for();
      auto d = 0.5 * (sc_->runner_config()->root_contract_config->proxy_delay_before_close() -
                      sc_->runner_config()->root_contract_config->client_delay_before_close());

      if (waiting_for > d) {
        LOG(INFO) << "sending close(I) for proxy " << proxy_sc_address().rserialize(true);
        auto msg = run_close();
        runner->cocoon_wallet()->send_transaction(sc_->address(), to_nano(0.7), {}, std::move(msg),
                                                  runner->create_proxy_sc_request_promise(shared_ptr()));
      }
      return ClientCheckResult::Ok;
    }
  }

  /*if (need_ton_top_up()) {
    auto msg = run_ton_top_up();
    runner->cocoon_wallet()->send_transaction(sc_->address(), to_nano(0.7), {}, std::move(msg),
                                              runner->create_proxy_sc_request_promise(shared_ptr()));
    return ClientCheckResult::Ok;
  }*/

  if (exp_sc_secret_hash() != runner->secret_hash()) {
    LOG(INFO) << "updating secret hash for proxy " << proxy_sc_address().rserialize(true);
    auto msg = run_change_secret_hash(runner->secret_hash());
    runner->cocoon_wallet()->send_transaction(sc_->address(), to_nano(0.7), {}, std::move(msg),
                                              runner->create_proxy_sc_request_promise(shared_ptr()));
    return ClientCheckResult::Ok;
  }

  td::int64 price_per_token = sc_->runner_config()->root_contract_config->price_per_token();
  if (price_per_token == 0) {
    return ClientCheckResult::Ok;
  }

  td::int64 exp_sc_stake_tokens_ = exp_sc_stake_ / price_per_token;
  if (exp_sc_tokens_payed_ - exp_sc_tokens_used_ < exp_sc_stake_tokens_ / 2) {
    LOG(INFO) << "topping up balance for proxy " << proxy_sc_address().rserialize(true)
              << " exp_available_tokens=" << exp_available_tokens();
    auto coins = exp_sc_stake_ - exp_sc_stake_ % price_per_token;
    auto msg = run_top_up(coins);
    runner->cocoon_wallet()->send_transaction(sc_->address(), to_nano(0.7) + coins, {}, std::move(msg),
                                              runner->create_proxy_sc_request_promise(shared_ptr()));
    return ClientCheckResult::Ok;
  }

  if (tokens_used_proxy_max_ > exp_sc_tokens_used_ + exp_sc_stake_tokens_ / 2 &&
      tokens_used_proxy_committed_to_blockchain_ > exp_sc_tokens_used_) {
    LOG(INFO) << "forcing charge for proxy " << proxy_sc_address().rserialize(true)
              << " uncharged=" << (tokens_used_proxy_max_ - exp_sc_tokens_used_);
    auto msg = run_charge();
    runner->cocoon_wallet()->send_transaction(sc_->address(), to_nano(0.7), {}, std::move(msg),
                                              runner->create_proxy_sc_request_promise(shared_ptr()));
    return ClientCheckResult::Ok;
  }

  return ClientCheckResult::Ok;
}

void ClientProxyInfo::process_signed_payment_data(cocoon_api::proxy_SignedPayment &data) {
  if (data.get_id() == cocoon_api::proxy_signedPaymentEmpty::ID) {
    return;
  }

  auto &d = static_cast<cocoon_api::proxy_signedPayment &>(data);

  auto R = sc_->check_signed_pay_message(d.data_.as_slice());
  if (R.is_error()) {
    LOG(ERROR) << "received incorrect signed pay message: " << R.move_as_error();
    return;
  }

  auto tokens = R.move_as_ok();

  if (tokens > tokens_used_proxy_committed_to_blockchain_) {
    tokens_used_proxy_committed_to_blockchain_ = tokens;
    signed_charge_message_ = td::UniqueSlice(d.data_.as_slice());
  }
}

void ClientProxyInfo::store_stats(td::StringBuilder &sb) {
  auto price_per_token = sc_->runner_config()->root_contract_config->price_per_token();
  sb << "<table>\n";
  sb << "<tr><td>proxy sc address</td><td>" << sc_->runner()->address_link(sc_->proxy_sc_address()) << "</td></tr>\n";
  sb << "<tr><td>proxy public key</td><td>" << sc_->proxy_public_key().to_hex() << "</td></tr>\n";
  sb << "<tr><td>sc address</td><td>" << sc_->runner()->address_link(sc_->address()) << "</td></tr>\n";
  sb << "<tr><td>state</td><td>" << sc_->state_to_string(sc_state_) << " " << sc_->state_to_string(exp_sc_state_);
  if (!exp_sc_is_closed()) {
    sb << " (<a href=\"/request/close?proxy=" << proxy_sc_address().rserialize(true) << "\">close now</a>)";
  }
  sb << "</td></tr>\n";
  {
    sb << "<tr><td>actions</td><td>";
    if (tokens_used_proxy_committed_to_blockchain_ > exp_sc_tokens_used_) {
      sb << "<a href=\"/request/charge?proxy=" << proxy_sc_address().rserialize(true) << "\">charge "
         << to_ton((tokens_used_proxy_committed_to_blockchain_ - exp_sc_tokens_used_) * price_per_token)
         << " ton right now</a><br/>\n";
    } else {
      sb << "nothing to charge right now<br/>\n";
    }
    sb << "<a href=\"/request/topup?proxy=" << proxy_sc_address().rserialize(true) << "\">top up "
       << to_ton(exp_sc_stake_) << " ton right now</a><br/>\n";
    if (sc_balance_ > exp_sc_stake_) {
      sb << "<a href=\"/request/withdraw?proxy=" << proxy_sc_address().rserialize(true) << "\">withdraw "
         << to_ton(sc_balance_ - exp_sc_stake_) << "ton right now</a><br/>\n";
    } else {
      sb << "nothing to withdraw right now<br/>\n";
    }
    sb << "</td></tr>\n";
    sb << "<tr><td>tokens</td><td>";
    sb << "payed up to:      " << sc_tokens_payed_ << "/" << exp_sc_tokens_payed_ << " tokens ("
       << to_ton(sc_tokens_payed_ * price_per_token) << "/" << to_ton(exp_sc_tokens_payed_ * price_per_token)
       << " ton)<br/>\n";
    sb << "charged up to:    " << sc_tokens_used_ << "/" << exp_sc_tokens_used_ << " tokens ("
       << to_ton(sc_tokens_used_ * price_per_token) << "/" << to_ton(exp_sc_tokens_used_ * price_per_token)
       << " ton)<br/>\n";
    sb << "used up to:    " << tokens_used_proxy_max_ << "/" << tokens_used_proxy_committed_to_db_ << "/"
       << tokens_used_proxy_committed_to_blockchain_ << " tokens (" << to_ton(tokens_used_proxy_max_ * price_per_token)
       << "/" << to_ton(tokens_used_proxy_committed_to_db_ * price_per_token) << "/"
       << to_ton(tokens_used_proxy_committed_to_blockchain_ * price_per_token) << " ton)<br/>\n";
    sb << "can use up to:    " << std::min(sc_tokens_payed_, sc_tokens_used_ + safe_div(sc_stake_, price_per_token))
       << "/" << std::min(exp_sc_tokens_payed_, exp_sc_tokens_used_ + safe_div(exp_sc_stake_, price_per_token))
       << " tokens<br/>\n";
    sb << "to charge:        " << (tokens_used_proxy_max_ - exp_sc_tokens_used_) << "/"
       << (tokens_used_proxy_committed_to_db_ - exp_sc_tokens_used_) << "/"
       << (tokens_used_proxy_committed_to_blockchain_ - exp_sc_tokens_used_) << " tokens ("
       << to_ton((tokens_used_proxy_max_ - exp_sc_tokens_used_) * price_per_token) << "/"
       << to_ton((tokens_used_proxy_committed_to_db_ - exp_sc_tokens_used_) * price_per_token) << "/"
       << to_ton((tokens_used_proxy_committed_to_blockchain_ - exp_sc_tokens_used_) * price_per_token) << " ton)";
    sb << "<br/>\n";
    sb << "automatic pay at: " << (exp_sc_tokens_used_ + safe_div(exp_sc_stake_, price_per_token) / 2)
       << " tokens used<br/>\n";
    sb << "</td></tr>\n";
  }
  sb << "<tr><td>stake</td><td>" << sc_->stake() << " ton</td></tr>\n";
  sb << "<tr><td>running requests</td><td>" << requests_running() << "</td></tr>\n";
  sb << "<tr><td>sc request is running</td><td>" << (sc_request_running_ ? "YES" : "NO") << "</td></tr>\n";
  sb << "</table>\n";
}

void ClientProxyInfo::store_stats(SimpleJsonSerializer &jb) {
  jb.add_element("proxy_sc_address", proxy_sc_address().rserialize(true));
  jb.add_element("proxy_public_key", proxy_public_key().to_hex());
  jb.add_element("sc_address", sc_->address().rserialize(true));
  jb.add_element("state", sc_->state());
  jb.add_element("tokens_used_proxy_committed_to_blockchain", tokens_used_proxy_committed_to_blockchain_);
  jb.add_element("tokens_used_proxy_committed_to_db", tokens_used_proxy_committed_to_db_);
  jb.add_element("tokens_used_proxy_max", tokens_used_proxy_max_);
  jb.add_element("tokens_charged", sc_tokens_used_);
  jb.add_element("tokens_payed", sc_tokens_payed_);
}

td::Ref<vm::Cell> ClientProxyInfo::run_charge() {
  CHECK(!sc_request_running_);

  sc_request_running_ = true;
  return sc_->repack_signed_pay_message(signed_charge_message_.as_slice(), sc_->runner()->cocoon_wallet_address(),
                                        &sc_request_qid_);
}

}  // namespace cocoon
