//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/net/Pipe.h"
#include "utils.h"
#include "td/actor/actor.h"
#include "td/actor/coro_task.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

namespace td {

extern int VERBOSITY_NAME(proxy);

class TransparentProxy : public TaskActor<td::BufferedFd<td::SocketFd>> {
 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;

    virtual void on_connected() = 0;
  };

  TransparentProxy(SocketFd socket_fd, IPAddress ip_address, string username, string password,
                   unique_ptr<Callback> callback, td::actor::ActorShared<> parent);

 protected:
  SocketPipe sfd_;
  IPAddress ip_address_;
  string username_;
  string password_;
  unique_ptr<Callback> callback_;
  td::actor::ActorShared<> parent_;

  void start_up() override;
  void hangup() override;

  Task<Action> task_loop_once() override;
  Task<BufferedFd<SocketFd>> finish(td::Status status) override;
  void alarm() override;

  virtual td::Result<Action> loop_impl() = 0;
  actor::Task<td::Unit> finish();
};

}  // namespace td
