#include "ProxyWorkerInfo.h"
#include "ProxyRunner.hpp"
#include "auto/tl/cocoon_api.h"
#include "tl/TlObject.h"

namespace cocoon {

ProxyWorkerInfo::ProxyWorkerInfo(ProxyRunner *runner, const block::StdAddress &worker_owner_address,
                                 std::shared_ptr<RunnerConfig> runner_config)
    : runner_(runner), worker_owner_address_(worker_owner_address) {
  worker_sc_address_ =
      runner_->generate_worker_sc_address(runner_->public_key(), runner_->owner_address(),
                                          runner_->sc_address(runner_config), worker_owner_address, runner_config);
}

ProxyWorkerInfo::ProxyWorkerInfo(ProxyRunner *runner, const cocoon_api::proxyDb_workerInfo &c,
                                 std::shared_ptr<RunnerConfig> runner_config)
    : runner_(runner) {
  CHECK(runner->rdeserialize(worker_owner_address_, c.owner_address_));
  worker_sc_address_ =
      runner_->generate_worker_sc_address(runner_->public_key(), runner_->owner_address(),
                                          runner_->sc_address(runner_config), worker_owner_address_, runner_config);

  td::int64 sc_tokens, tokens;
  int last_request_at;

  sc_tokens = c.sc_tokens_;
  tokens = c.tokens_;
  last_request_at = c.last_request_at_;

  signed_payments_.incr_tokens(sc_tokens);
  update_balance(sc_tokens);
  adjust_balance(tokens - sc_tokens);
  committed_to_db(runner_->last_saved_state_seqno());
  committed_to_blockchain(runner_->last_saved_state_seqno());
  CHECK(tokens_committed_to_blockchain() == tokens);
  CHECK(tokens_committed_to_db() == tokens);
  CHECK(tokens_max() == tokens);
  set_last_request_at(last_request_at);
  updated_from_db_ = false;
}

ClientCheckResult ProxyWorkerInfo::check() {
  if (paying_now()) {
    return ClientCheckResult::Ok;
  }

  auto delta = tokens_ready_to_pay();

  if (delta * runner_->worker_fee_per_token() >= runner_->min_worker_payout_sum()) {
    runner_->worker_payout(*this, false);
  }
  return ClientCheckResult::Ok;
}

ton::tl_object_ptr<cocoon_api::worker_paymentStatus> ProxyWorkerInfo::serialize_payment_status() {
  runner_->sign_worker_payment(*this);

  return ton::create_tl_object<cocoon_api::worker_paymentStatus>(
      signed_payment(), signed_payments_.tokens_committed_to_db(), signed_payments_.tokens_max());
}

void ProxyWorkerInfo::store_stats(td::StringBuilder &sb, td::int64 worker_fee_per_token) {
  sb << "<table>\n";
  sb << "<tr><td>owner address</td><td>" << runner_->address_link(worker_owner_address()) << "</td></tr>\n";
  sb << "<tr><td>sc address</td><td>" << runner_->address_link(worker_sc_address()) << "</td></tr>\n";
  sb << "<tr><td>earned tokens</td><td>" << tokens_committed_to_blockchain() << "/" << tokens_committed_to_db() << "/"
     << tokens_max() << "</td></tr>\n";
  sb << "<tr><td>unpayed tokens</td><td>" << tokens_ready_to_pay() << "/" << tokens_max_to_pay() << " (~"
     << to_ton(tokens_ready_to_pay() * worker_fee_per_token) << " TON)";
  if (tokens_ready_to_pay() > 0) {
    sb << " <a href=\"/request/payout?worker=" << worker_owner_address().rserialize(true) << "\">pay now</a>";
  }
  sb << "</td></tr>\n";
  sb << "<tr><td>signed tokens</td><td>" << signed_payment_tokens() << "</td></tr>\n";
  sb << "<tr><td>paying now</td><td>" << (paying_now() ? "YES" : "NO") << "</td></tr>\n";
  sb << "<tr><td>running queries</td><td>" << running_queries() << "</td></tr>\n";
  sb << "</table>\n";
}

void ProxyWorkerInfo::store_stats(SimpleJsonSerializer &jb) {
  jb.start_object();
  jb.add_element("owner_address", worker_owner_address().rserialize(true));
  jb.add_element("sc_address", worker_sc_address().rserialize(true));
  jb.add_element("earned_tokens", tokens());
  jb.add_element("unpayed_tokens", tokens_ready_to_pay());
  jb.add_element("paying_now", paying_now());
  jb.add_element("running_queries", running_queries());
  jb.stop_object();
}

}  // namespace cocoon
