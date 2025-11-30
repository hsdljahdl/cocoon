//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/net/SslCtx.h"

#include "td/utils/ByteFlow.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

namespace detail {
class SslStreamImpl;
}  // namespace detail

class SslStream {
 public:
  SslStream();
  SslStream(SslStream &&) noexcept;
  SslStream &operator=(SslStream &&) noexcept;
  ~SslStream();

  static Result<SslStream> create(CSlice host, SslCtx ssl_ctx, bool use_ip_address_as_host = false);
  static Result<SslStream> create_server(SslCtx ssl_ctx);

  ByteFlowInterface &read_byte_flow();
  ByteFlowInterface &write_byte_flow();

  size_t flow_read(MutableSlice slice);
  size_t flow_write(Slice slice);

  explicit operator bool() const noexcept {
    return static_cast<bool>(impl_);
  }

 private:
  unique_ptr<detail::SslStreamImpl> impl_;

  explicit SslStream(unique_ptr<detail::SslStreamImpl> impl);
};

class SslStreamHelper {
 public:
  SslStreamHelper(ChainBufferReader &reader, ChainBufferWriter &writer, SslStream ssl_stream);
  td::Status read_loop();
  td::Status write_loop();

  td::Status loop();
  ChainBufferReader &input_buffer();
  ChainBufferWriter &output_buffer();

 private:
  ByteFlowSource read_source_;
  ByteFlowSink read_sink_;

  ChainBufferWriter write_buffer_;
  ChainBufferReader write_buffer_reader_{write_buffer_.extract_reader()};
  ByteFlowSource write_source_{&write_buffer_reader_};
  ByteFlowMoveSink write_sink_;

  SslStream ssl_stream_;
};

}  // namespace td
