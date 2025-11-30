#pragma once

#include "common/bitstring.h"
#include "td/utils/Status.h"
#include "vm/cells/CellBuilder.h"
#include "vm/cells/CellSlice.h"
#include "crypto/block/block.h"

namespace cocoon {

bool fetch_coins(vm::CellSlice &cs, td::uint64 &to);
bool fetch_address(vm::CellSlice &cs, block::StdAddress &addr, bool is_test, bool is_bouncable);

void store_coins(vm::CellBuilder &cb, td::uint64 total);
void store_address(vm::CellBuilder &cb, const block::StdAddress &addr);

}  // namespace cocoon
