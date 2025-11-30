#pragma once
#include "tdx.h"
#include "td/actor/actor.h"
#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/optional.h"
#include "td/actor/coro_task.h"
#include "td/net/FramedPipe.h"
#include "td/net/Pipe.h"
#include "td/net/utils.h"

namespace td {
class SslStream;
}  // namespace td
namespace cocoon {
// Create a server-side SSL stream using provided cert/key and policy verification
td::Result<td::SslStream> create_server_ssl_stream(tdx::CertAndKey cert_and_key, tdx::PolicyRef policy);

// Create a client-side SSL stream for the given host using provided cert/key and policy verification
td::Result<td::SslStream> create_client_ssl_stream(td::CSlice host, tdx::CertAndKey cert_and_key, tdx::PolicyRef policy,
                                                   bool enable_sni = true);

// Re-export framed I/O functions from td namespace for backward compatibility
using td::framed_read;
using td::framed_write;

/**
 * @brief Write a TL-serialized object with framing
 * @tparam T Type with store() method for TL serialization
 * @param writer Buffer to write to
 * @param object Object to serialize and write
 * @return Status indicating success or error
 */
template <class T>
td::Status framed_tl_write(td::ChainBufferWriter &writer, const T &object) {
  auto serialized = td::serialize(object);
  return td::framed_write(writer, serialized);
}

/**
 * @brief Read a TL-serialized object with framing
 * @tparam T Type with parse() method for TL deserialization
 * @param reader Buffer to read from
 * @return Optional containing the deserialized object if successful, or error status
 *         Returns empty optional if more data is needed (non-error case)
 */
template <class T>
td::Result<td::optional<T>> framed_tl_read(td::ChainBufferReader &reader) {
  td::BufferSlice message;
  TRY_RESULT(needed, td::framed_read(reader, message));

  // If needed > 0, we need more data (not an error)
  if (needed > 0) {
    return td::optional<T>{};
  }

  // Deserialize the message
  T object;
  TRY_STATUS(td::unserialize(object, message.as_slice()));
  return td::optional<T>(std::move(object));
}

// Move all available data from reader to writer
template <class L, class R>
void proxy_sockets(L &reader, R &writer) {
  // NB: do not call output_buffer() if there is nothing to write
  if (reader.input_buffer().empty()) {
    return;
  }
  writer.output_buffer().append(reader.input_buffer());
}

td::actor::StartedTask<td::BufferedFd<td::SocketFd>> socks5(td::SocketFd socket_fd, td::IPAddress dest,
                                                            td::string username, td::string password);
td::actor::StartedTask<td::Unit> proxy(td::Slice name, td::Pipe left, td::Pipe right);

td::actor::Task<std::pair<td::Pipe, tdx::AttestationData>> wrap_tls_client(td::Slice name, td::Pipe pipe,
                                                                           tdx::CertAndKey cert_and_key,
                                                                           tdx::PolicyRef policy);
td::actor::Task<std::pair<td::Pipe, tdx::AttestationData>> wrap_tls_server(td::Slice name, td::Pipe pipe,
                                                                           tdx::CertAndKey cert_and_key,
                                                                           tdx::PolicyRef policy);

}  // namespace cocoon