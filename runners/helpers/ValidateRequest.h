#pragma once

#include "td/utils/buffer.h"
#include "td/utils/Status.h"

namespace cocoon {

td::Result<td::BufferSlice> validate_modify_request(std::string url, td::BufferSlice request, std::string *model,
                                                    td::int64 *max_tokens);

}
