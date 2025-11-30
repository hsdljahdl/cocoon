/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#pragma once

#include "auto/tl/cocoon_api.h"

namespace cocoon {
td::BufferSlice cocoon_serialize_tl_object(const cocoon_api::Object *T, bool boxed);
td::BufferSlice cocoon_serialize_tl_object(const cocoon_api::Function *T, bool boxed);
td::BufferSlice cocoon_serialize_tl_object(const cocoon_api::Object *T, bool boxed, td::BufferSlice &&suffix);
td::BufferSlice cocoon_serialize_tl_object(const cocoon_api::Function *T, bool boxed, td::BufferSlice &&suffix);
td::BufferSlice cocoon_serialize_tl_object(const cocoon_api::Object *T, bool boxed, td::Slice suffix);
td::BufferSlice cocoon_serialize_tl_object(const cocoon_api::Function *T, bool boxed, td::Slice suffix);

td::UInt256 get_tl_object_sha256(const cocoon_api::Object *T);

template <class Tp, std::enable_if_t<std::is_base_of<cocoon_api::Object, Tp>::value>>
td::UInt256 get_tl_object_sha256(const Tp &T) {
  return get_tl_object_sha256(static_cast<const cocoon_api::Object *>(&T));
}
}  // namespace cocoon

#include "cocoon-common-utils.hpp"
