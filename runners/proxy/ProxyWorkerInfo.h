#pragma once

#include "runners/BaseRunner.hpp"
#include "ProxySignedPayments.hpp"
#include "tl/TlObject.h"

namespace cocoon {

class ProxyRunner;

class ProxyWorkerInfo : public std::enable_shared_from_this<ProxyWorkerInfo> {
 public:
  ProxyWorkerInfo(ProxyRunner *runner, const block::StdAddress &worker_owner_address,
                  std::shared_ptr<RunnerConfig> runner_config);
  ProxyWorkerInfo(ProxyRunner *runner, const cocoon_api::proxyDb_workerInfo &c,
                  std::shared_ptr<RunnerConfig> runner_config);

  std::shared_ptr<ProxyWorkerInfo> shared_ptr() {
    return shared_from_this();
  }

  const auto &worker_owner_address() const {
    return worker_owner_address_;
  }
  const auto &worker_sc_address() const {
    return worker_sc_address_;
  }
  auto runner() const {
    return runner_;
  }
  auto tokens_committed_to_blockchain() const {
    return signed_payments_.tokens_committed_to_blockchain();
  }
  auto tokens_committed_to_db() const {
    return signed_payments_.tokens_committed_to_db();
  }
  auto tokens_max() const {
    return signed_payments_.tokens_max();
  }
  bool has_signed_payment() const {
    return signed_payments_.has_signed_payment();
  }
  td::Slice signed_payment_data() const {
    return signed_payments_.signed_payment_data();
  }
  auto signed_payment_tokens() const {
    return signed_payments_.tokens_committed_to_blockchain();
  }
  ton::tl_object_ptr<cocoon_api::proxy_SignedPayment> signed_payment() const {
    if (has_signed_payment()) {
      return create_tl_object<cocoon_api::proxy_signedPayment>(td::BufferSlice(signed_payment_data()));
    } else {
      return create_tl_object<cocoon_api::proxy_signedPaymentEmpty>();
    }
  }
  bool paying_now() const {
    return paying_now_;
  }
  auto tokens_ready_to_pay() const {
    return tokens_committed_to_blockchain() - exp_sc_tokens_;
  }
  auto tokens_max_to_pay() const {
    return tokens_max() - exp_sc_tokens_;
  }
  auto tokens() const {
    return tokens_max();
  }
  auto running_queries() const {
    return running_queries_;
  }
  auto last_request_at() const {
    return last_request_at_;
  }
  bool need_to_write() const {
    return updated_from_db_;
  }
  bool is_closed() const {
    return false;
  }

  ton::tl_object_ptr<cocoon_api::proxyDb_workerInfo> serialize() const {
    return cocoon::create_tl_object<cocoon_api::proxyDb_workerInfo>(worker_owner_address_.rserialize(true),
                                                                    worker_sc_address_.rserialize(true), sc_tokens_,
                                                                    tokens(), last_request_at_);
  }

  ton::tl_object_ptr<cocoon_api::worker_paymentStatus> serialize_payment_status();

  void update_balance(td::int64 new_tokens) {
    if (sc_tokens_ >= new_tokens) {
      return;
    }

    sc_tokens_ = new_tokens;
    if (exp_sc_tokens_ < new_tokens) {
      exp_sc_tokens_ = new_tokens;
    }
    CHECK(sc_tokens_ <= tokens());
    updated_from_db_ = true;
  }

  void adjust_balance(td::int64 tokens_used) {
    signed_payments_.incr_tokens(tokens_used);
    updated_from_db_ = true;
  }

  void forwarded_query() {
    running_queries_++;
  }

  void forwarded_query_failed(double work_time) {
    running_queries_--;
  }

  void forwarded_query_success(double work_time) {
    running_queries_--;
  }

  void forwarded_query_error(double work_time) {
    running_queries_--;
  }

  void set_last_request_at(td::int32 value) {
    last_request_at_ = value;
    updated_from_db_ = true;
  }

  void set_last_request_at() {
    last_request_at_ = (td::int32)std::time(0);
    updated_from_db_ = true;
  }

  void update_signed_payment_data(td::int64 tokens, td::UniqueSlice data) {
    signed_payments_.set_signed_payment(tokens, std::move(data));
  }

  void committed_to_db(td::int32 seqno) {
    signed_payments_.committed_to_db(seqno);
  }

  void committed_to_blockchain(td::int32 seqno) {
    signed_payments_.committed_to_blockchain(seqno);
  }

  void pay_out(td::int64 tokens) {
    paying_now_ = true;
    exp_sc_tokens_ = tokens;
  }

  void pay_out_completed() {
    paying_now_ = false;
  }

  void written_to_db() {
    updated_from_db_ = false;
  }

  void store_stats(td::StringBuilder &sb, td::int64 worker_fee_per_token);
  void store_stats(SimpleJsonSerializer &jb);

  ClientCheckResult check();

 private:
  ProxyRunner *runner_;

  block::StdAddress worker_owner_address_;
  block::StdAddress worker_sc_address_;

  td::int64 sc_tokens_{0};

  td::int64 exp_sc_tokens_{0};
  bool paying_now_{false};

  ProxySignedPayments signed_payments_;

  bool updated_from_db_{true};

  td::int32 running_queries_{0};
  td::int32 last_request_at_{0};
};

}  // namespace cocoon
