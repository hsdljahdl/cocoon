#include "ProxyClientInfo.h"
#include "ProxyRunner.hpp"
#include "checksum.h"
#include "runners/smartcontracts/ClientContract.hpp"

#include "auto/tl/cocoon_api.h"
#include "auto/tl/cocoon_api.hpp"
#include "common/bitstring.h"
#include "td/utils/overloaded.h"

namespace cocoon {

ProxyClientInfo::ProxyClientInfo(ProxyRunner *runner, const block::StdAddress &client_owner_address,
                                 std::shared_ptr<RunnerConfig> runner_config)
    : runner_(runner), client_owner_address_(client_owner_address) {
  client_sc_address_ =
      runner_->generate_client_sc_address(runner_->public_key(), runner_->owner_address(),
                                          runner_->sc_address(runner_config), client_owner_address, runner_config);

  if (runner_->ton_disabled()) {
    update_state(0, to_nano(100), to_nano(1), 0, runner_->runner_config()->root_contract_config->price_per_token(),
                 td::sha256_bits256(""));
  }
}

ProxyClientInfo::ProxyClientInfo(ProxyRunner *runner, const cocoon_api::proxyDb_ClientInfo &cr,
                                 std::shared_ptr<RunnerConfig> runner_config)
    : runner_(runner) {
  auto price_per_token = runner_config->root_contract_config->price_per_token();
  td::int64 sc_tokens_used, balance, tokens_used, sc_stake = to_nano(1.0);
  td::Bits256 secret_hash;
  td::int32 sc_status, last_request_at;
  cocoon_api::downcast_call(const_cast<cocoon_api::proxyDb_ClientInfo &>(cr),
                            td::overloaded(
                                [&](const cocoon_api::proxyDb_clientInfo &c) {
                                  CHECK(runner->rdeserialize(client_owner_address_, c.owner_address_));
                                  sc_status = c.status_;
                                  sc_tokens_used = c.sc_tokens_used_;
                                  balance = c.balance_;
                                  tokens_used = c.tokens_used_;
                                  secret_hash = c.secret_hash_;
                                  last_request_at = c.last_request_at_;
                                },
                                [&](const cocoon_api::proxyDb_clientInfoV2 &c) {
                                  CHECK(runner->rdeserialize(client_owner_address_, c.owner_address_));
                                  sc_status = c.status_;
                                  sc_tokens_used = c.sc_tokens_used_;
                                  balance = c.balance_;
                                  tokens_used = c.tokens_used_;
                                  secret_hash = c.secret_hash_;
                                  sc_stake_ = c.stake_;
                                  last_request_at = c.last_request_at_;
                                }));
  deduct(sc_tokens_used);
  update_balance(balance, sc_tokens_used, price_per_token);
  deduct(tokens_used - sc_tokens_used);
  sc_secret_hash_ = secret_hash;
  sc_stake_ = sc_stake;
  sc_status_ = sc_status;
  sc_tokens_stake_ = safe_div(sc_stake_, price_per_token);

  committed_to_db(runner_->last_saved_state_seqno());
  committed_to_blockchain(runner_->last_saved_state_seqno());
  updated_from_db_ = false;

  client_sc_address_ =
      runner->generate_client_sc_address(runner_->public_key(), runner_->owner_address(),
                                         runner_->sc_address(runner_config), client_owner_address_, runner_config);
}

ClientCheckResult ProxyClientInfo::check() {
  if (charging_now()) {
    return ClientCheckResult::Ok;
  }

  if (is_closed()) {
    if (running_queries() > 0) {
      return ClientCheckResult::Ok;
    }

    return ClientCheckResult::Ok;
  }

  if (is_closing() && tokens_max() == tokens_committed_to_blockchain() && running_queries() == 0) {
    runner_->client_charge(*this, true);
    return ClientCheckResult::Ok;
  } else {
    auto delta = tokens_ready_to_charge();
    if (delta == 0) {
      return ClientCheckResult::Ok;
    }
    if (delta * runner_->price_per_token() >= runner_->min_client_charge_sum()) {
      runner_->client_charge(*this, false);
      return ClientCheckResult::Ok;
    }
  }

  return ClientCheckResult::Ok;
}

ton::tl_object_ptr<cocoon_api::client_paymentStatus> ProxyClientInfo::serialize_payment_status() {
  runner_->sign_client_payment(*this);

  return ton::create_tl_object<cocoon_api::client_paymentStatus>(
      signed_payment(), signed_payments_.tokens_committed_to_db(), signed_payments_.tokens_max());
}

void ProxyClientInfo::store_stats(td::StringBuilder &sb, td::int64 price_per_token) {
  sb << "<table>\n";
  sb << "<tr><td>owner address</td><td>" << runner_->address_link(client_owner_address()) << "</td></tr>\n";
  sb << "<tr><td>sc address</td><td>" << runner_->address_link(client_sc_address()) << "</td></tr>\n";
  sb << "<tr><td>used tokens</td><td>" << tokens_used() << "</td></tr>\n";
  sb << "<tr><td>tokens to charge</td><td>" << tokens_ready_to_charge() << "/" << tokens_max_to_charge() << " (~"
     << to_ton(tokens_ready_to_charge() * price_per_token) << ")";
  if (tokens_ready_to_charge()) {
    sb << " <a href=\"/request/charge?client=" << client_owner_address().rserialize(true) << "\">charge now</a>";
  }
  sb << "</td></tr>\n";
  sb << "<tr><td>available tokens</td><td>" << tokens_available() << "</td></tr>\n";
  sb << "<tr><td>reserved tokens</td><td>" << tokens_reserved_ << "</td></tr>\n";
  sb << "<tr><td>signed tokens</td><td>" << signed_payment_tokens() << "</td></tr>\n";
  sb << "<tr><td>running queries</td><td>" << running_queries() << "</td></tr>\n";
  sb << "<tr><td>closing</td><td>" << (is_closing() ? "YES" : "NO") << "</td></tr>\n";
  sb << "<tr><td>closed</td><td>" << (is_closed() ? "YES" : "NO") << "</td></tr>\n";
  sb << "</table>\n";
}

void ProxyClientInfo::store_stats(SimpleJsonSerializer &jb) {
  jb.start_object();
  jb.add_element("owner_address", client_owner_address().rserialize(true));
  jb.add_element("sc_address", client_sc_address().rserialize(true));
  jb.add_element("used_tokens", tokens_used());
  jb.add_element("tokens_to_charge", tokens_ready_to_charge());
  jb.add_element("available_tokens", tokens_available());
  jb.add_element("reserved_tokens", tokens_reserved_);
  jb.add_element("running_queries", running_queries());
  jb.add_element("closing", is_closing());
  jb.add_element("closed", is_closed());
  jb.stop_object();
}

}  // namespace cocoon
