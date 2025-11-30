#pragma once

#include "runners/BaseRunner.hpp"

namespace cocoon {

struct ProxyConnectingClient : public std::enable_shared_from_this<ProxyConnectingClient> {
  ProxyConnectingClient(const block::StdAddress &owner_address, const block::StdAddress &smartcontract,
                        td::uint64 nonce, TcpClient::ConnectionId connection_id)
      : owner_address(std::move(owner_address))
      , smartcontract(std::move(smartcontract))
      , nonce(nonce)
      , connection_id(connection_id) {
  }
  block::StdAddress owner_address;
  block::StdAddress smartcontract;
  td::uint64 nonce;
  bool received{false};
  td::Promise<td::BufferSlice> promise;
  TcpClient::ConnectionId connection_id;
};

}  // namespace cocoon
