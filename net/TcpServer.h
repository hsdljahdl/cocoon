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

#include "td/actor/actor.h"
#include "td/actor/common.h"
#include "td/utils/buffer.h"
#include <memory>

namespace cocoon {

class TcpServer : public td::actor::Actor {
 public:
  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void on_ready() = 0;
    virtual void on_stop_ready() = 0;
    virtual void receive_message(td::BufferSlice message) = 0;
    virtual void receive_query(td::BufferSlice message, td::Promise<td::BufferSlice> promise) = 0;
  };
  virtual ~TcpServer() = default;
  static td::actor::ActorOwn<TcpServer> create(td::uint16 port, std::unique_ptr<Callback> callback);
};

}  // namespace cocoon
