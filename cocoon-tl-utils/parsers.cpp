#include "parsers.hpp"
#include "common/bitstring.h"
#include "common/refcnt.hpp"
#include "common/refint.h"

namespace cocoon {

bool fetch_coins(vm::CellSlice &cs, td::uint64 &to) {
  td::uint32 len;
  if (!cs.fetch_uint_to(4, len)) {
    return false;
  }

  if (len > 8) {
    return false;
  }

  if (len == 0) {
    to = 0;
    return true;
  }

  if (!cs.have(len * 8)) {
    return false;
  }

  td::BitArray<64> tmp;
  tmp.set_zero();
  CHECK(cs.fetch_bytes(tmp.as_slice().remove_prefix(8 - len)));

  to = tmp.to_ulong();
  return true;
}

void store_coins(vm::CellBuilder &cb, td::uint64 total) {
  if (total == 0) {
    cb.store_zeroes(4);
    return;
  }
  auto x = total;
  int cnt = 0;
  while (x > 0) {
    x >>= 8;
    cnt++;
  }
  cb.store_long(cnt, 4);
  cb.store_long(total, 8 * cnt);
}

bool fetch_address(vm::CellSlice &cs, block::StdAddress &addr, bool is_test, bool is_bouncable) {
  td::uint64 tmp;
  if (!cs.fetch_uint_to(3, tmp) || tmp != 4) {
    addr.invalidate();
    return false;
  }
  if (!cs.fetch_uint_to(8, addr.workchain) || !cs.fetch_bytes(addr.addr.as_slice())) {
    addr.invalidate();
    return false;
  }
  addr.bounceable = is_bouncable;
  addr.testnet = is_test;
  return true;
}

void store_address(vm::CellBuilder &cb, const block::StdAddress &addr) {
  cb.store_long(2, 2).store_long(0, 1).store_long(addr.workchain, 8).store_bytes(addr.addr.as_slice());
}

}  // namespace cocoon
