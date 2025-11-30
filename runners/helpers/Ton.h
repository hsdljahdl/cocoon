#pragma once

#include "td/utils/int_types.h"
#include "td/utils/logging.h"

namespace cocoon {

static constexpr td::int64 to_nano(double d) {
  return static_cast<td::int64>(d * 1e9);
}

static constexpr double to_ton(td::int64 value) {
  return (td::int32)(value / 1000000000) + 1e-9 * (int)(value % 1000000000);
}

static constexpr td::int64 safe_div(td::int64 a, td::int64 b) {
  return b > 0 ? a / b : 1000000000000ll;
}

static constexpr td::int64 adjust_tokens(td::int64 tokens_count, td::int64 coefficient, td::int32 mult) {
  return static_cast<td::int64>((double)tokens_count * ((double)coefficient * 0.001) * (double)mult * 0.0001);
}

static inline std::string address_link(td::Slice address, bool is_testnet = false) {
  const char* base_url = is_testnet ? "https://testnet.tonviewer.com/" : "https://tonviewer.com/";
  return PSTRING() << "<a href=\"" << base_url << address << "\" target=\"_blank\">" << address << "</a>";
}

}  // namespace cocoon
