#pragma once

#include "td/utils/SharedSlice.h"
#include <map>

namespace cocoon {

class ProxySignedPayments {
 public:
  td::int64 tokens_committed_to_blockchain() const {
    return tokens_committed_to_blockchain_;
  }
  td::int64 tokens_committed_to_db() const {
    if (seqno_to_tokens_.size() > 0) {
      return seqno_to_tokens_.rbegin()->second;
    } else {
      return tokens_committed_to_blockchain_;
    }
  }
  td::int64 tokens_max() const {
    return tokens_max_;
  }

  void incr_tokens(td::int64 tokens) {
    tokens_max_ += tokens;
  }

  void committed_to_db(td::int32 seqno) {
    if (tokens_max_ > tokens_committed_to_db()) {
      CHECK(seqno_to_tokens_.emplace(seqno, tokens_max_).second);
    }
  }

  void committed_to_blockchain(td::int32 seqno) {
    auto tokens = tokens_committed_to_blockchain_;
    auto it = seqno_to_tokens_.begin();
    while (it != seqno_to_tokens_.end() && it->first <= seqno) {
      tokens = it->second;
      it++;
    }
    seqno_to_tokens_.erase(seqno_to_tokens_.begin(), it);
    CHECK(tokens >= tokens_committed_to_blockchain_);
    if (tokens > tokens_committed_to_blockchain_) {
      tokens_committed_to_blockchain_ = tokens;
      signed_payment_.clear();
    }
  }

  bool has_signed_payment() const {
    return signed_payment_.size() != 0;
  }
  td::Slice signed_payment_data() const {
    return signed_payment_.as_slice();
  }

  void set_signed_payment(td::int64 tokens, td::UniqueSlice signed_payment) {
    CHECK(tokens == tokens_committed_to_blockchain_);
    signed_payment_ = std::move(signed_payment);
  }

 private:
  td::int64 tokens_committed_to_blockchain_{0};
  std::map<td::int32, td::int64> seqno_to_tokens_;
  td::int64 tokens_max_{0};
  td::UniqueSlice signed_payment_;
};

}  // namespace cocoon
