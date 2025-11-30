//
// Created by Arseny Smirnov  on 19/09/2025.
//

#pragma once
#include "td/net/Pipe.h"
#include "td/actor/coro_utils.h"
#include "td/actor/core/Actor.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"
#include "td/utils/UInt.h"
namespace td {
template <class T>
td::Status loop_read(td::Slice name, T &socket) {
  TRY_STATUS_PREFIX(socket.flush_read(), PSLICE() << "read from " << name << ": ");
  return td::Status::OK();
}
template <class T>
td::Status loop_write(td::Slice name, T &socket) {
  TRY_STATUS_PREFIX(socket.flush_write(), PSLICE() << "write to " << name << ": ");
  return td::Status::OK();
}

template <size_t size, class StorerT>
void store(UInt<size> x, StorerT &storer) {
  storer.store_binary(x);
}
template <size_t size, class ParserT>
void parse(UInt<size> &x, ParserT &parser) {
  x = parser.template fetch_binary<UInt<size>>();
}

using actor::Task;
template <class T>
using TaskActor = actor::TaskActor<T>;
using actor::spawn_task_actor;

}  // namespace td
