//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/TransparentProxy.h"

#include "td/net/Pipe.h"
#include "utils.h"
#include "td/utils/logging.h"
#include "td/utils/port/detail/PollableFd.h"

namespace td {

int VERBOSITY_NAME(proxy) = VERBOSITY_NAME(DEBUG);

TransparentProxy::TransparentProxy(SocketFd socket_fd, IPAddress ip_address, string username, string password,
                                   unique_ptr<Callback> callback, td::actor::ActorShared<> parent)
    : sfd_(make_socket_pipe(std::move(socket_fd)))
    , ip_address_(std::move(ip_address))
    , username_(std::move(username))
    , password_(std::move(password))
    , callback_(std::move(callback))
    , parent_(std::move(parent)) {
}

void TransparentProxy::hangup() {
  on_error(Status::Error("Canceled"));
}

Task<TransparentProxy::Action> TransparentProxy::task_loop_once() {
  co_await loop_read("", sfd_);
  auto action = co_await loop_impl();
  co_await loop_write("", sfd_);
  co_return action;
}

Task<td::BufferedFd<SocketFd>> TransparentProxy::finish(td::Status status) {
  if (status.is_error()) {
    VLOG(proxy) << "Receive " << status;
  }

  VLOG(proxy) << "Finish to connect to proxy";
  auto fd = co_await sfd_.extract_fd();

  co_await std::move(status);

  co_return fd;
}

void TransparentProxy::start_up() {
  VLOG(proxy) << "Begin to connect to proxy";
  sfd_.subscribe();
  alarm_timestamp() = td::Timestamp::in(10);
}

void TransparentProxy::alarm() {
  on_error(Status::Error("Connection timeout expired"));
}

}  // namespace td
