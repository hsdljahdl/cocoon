//
// Created by Arseny Smirnov  on 16.07.2025.
//
#include "cocoon/RevProxy.h"
#include "cocoon/pow.h"

#include "td/net/SslStream.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/optional.h"
#include "tdx.h"
#include "utils.h"
#include "td/net/Pipe.h"
#include "td/net/utils.h"
#include "td/utils/CancellationToken.h"

namespace cocoon {
namespace {
td::Task<td::Unit> accept_and_proxy(td::SocketFd socket, std::shared_ptr<const RevProxy::Config> config) {
  td::SocketPipe client_pipe = td::make_socket_pipe(std::move(socket));

  // Verify PoW from incoming client (always enabled)
  client_pipe = co_await pow::verify_pow_server(std::move(client_pipe), config->pow_difficulty);

  auto [tls_socket, info] =
      co_await wrap_tls_server("-Rev", std::move(client_pipe), config->cert_and_key_.load(), config->policy_);
  LOG(INFO) << "Rev proxy: TLS handshake complete, " << (info.is_empty() ? "no attestation" : "attestation verified");
  auto dst_pipe = make_socket_pipe(co_await td::SocketFd::open(config->dst_));

  if (config->serialize_info) {
    co_await framed_tl_write(dst_pipe.output_buffer(), info);
  }

  auto r_status = co_await proxy("-Rev", std::move(tls_socket), std::move(dst_pipe)).wrap();
  LOG_IF(INFO, r_status.is_error()) << "Rev proxy: connection closed with error: " << r_status.error();
  co_return td::Unit();
}
}  // namespace

void RevProxy::start_up() {
  struct Callback : public td::TcpListener::Callback {
    std::shared_ptr<const Config> config_;
    explicit Callback(std::shared_ptr<const Config> config) : config_(std::move(config)) {
    }
    void accept(td::SocketFd fd) override {
      accept_and_proxy(std::move(fd), config_).start().detach();
    }
  };
  listener_ = td::actor::create_actor<td::TcpInfiniteListener>("Listener", config_->src_port_,
                                                               std::make_unique<Callback>(config_));
}
}  // namespace cocoon
