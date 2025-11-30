#pragma once

#include "runners/BaseRunner.hpp"

namespace cocoon {

class WorkerRunner;

class WorkerProxyConnection : public ProxyOutboundConnection {
 public:
  WorkerProxyConnection(BaseRunner *runner, const RemoteAppType &remote_app_type, const td::Bits256 &remote_app_hash,
                        TcpClient::ConnectionId connection_id, TcpClient::TargetId target_id)
      : ProxyOutboundConnection(runner, remote_app_type, remote_app_hash, connection_id, target_id) {
  }
  void send_handshake() override;
  void received_handshake_answer(td::BufferSlice answer);
  void received_compare_answer(td::BufferSlice answer);
  void received_extended_compare_answer(td::BufferSlice answer);
  void send_handshake_complete();
  void received_handshake_complete_answer(td::BufferSlice answer);

  WorkerRunner *runner();

  const auto &proxy_sc_address_str() const {
    return proxy_sc_address_str_;
  }

 private:
  std::string proxy_sc_address_str_;
};

}  // namespace cocoon
