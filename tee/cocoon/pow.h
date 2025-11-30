// Minimal, transport-agnostic PoW helpers: operate on raw bytes only.
#pragma once

#include "td/actor/coro_task.h"
#include "td/net/Pipe.h"
#include "td/utils/optional.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"
#include "td/utils/UInt.h"
#include <cstdint>
#include <string>
#include <utility>

namespace cocoon::pow {

// PoW protocol: Server sends challenge, client responds with nonce
// Uses TL serialization with magic numbers:
//
// cocoon.powSimple#418e1291 difficulty_bits:int32 salt:UInt128 = cocoon.PowRequest;
// cocoon.powSimpleResponse#01827319 nonce:int64 = cocoon.PowResponse;

static constexpr td::uint32 POW_SIMPLE_MAGIC = 0x418e1291;
static constexpr td::uint32 POW_SIMPLE_RESPONSE_MAGIC = 0x01827319;

/**
 * PoW challenge sent by server to client.
 * Verification: SHA256(salt || nonce) must have <difficulty_bits> leading zero bits
 */
struct PowChallenge {
  td::int32 difficulty_bits{20};
  td::UInt128 salt;

  bool verify_response(td::int64 nonce) const;

  static PowChallenge make_challenge(td::int32 difficulty_bits);

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(POW_SIMPLE_MAGIC, storer);
    store(difficulty_bits, storer);
    store(salt, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    td::uint32 magic;
    parse(magic, parser);
    if (magic != POW_SIMPLE_MAGIC) {
      parser.set_error("Unexpected PoW magic");
      return;
    }
    parse(difficulty_bits, parser);
    parse(salt, parser);
  }
};

/**
 * PoW response sent by client to server.
 */
struct PowResponse {
  td::int64 nonce{0};

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(POW_SIMPLE_RESPONSE_MAGIC, storer);
    store(nonce, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    td::uint32 magic;
    parse(magic, parser);
    if (magic != POW_SIMPLE_RESPONSE_MAGIC) {
      parser.set_error("Unexpected PoW response magic");
      return;
    }
    parse(nonce, parser);
  }
};

struct PowSolver {
 public:
  td::optional<td::int64> solve(const PowChallenge &challenge);

 private:
  td::uint64 nonce_{0};
};

// Coroutine to verify PoW before allowing connection
// Returns the pipe on success, or error on failure
td::actor::StartedTask<td::SocketPipe> verify_pow_server(td::SocketPipe pipe, td::int32 difficulty_bits);

// Coroutine to solve PoW challenge from server
// Returns BufferedFd after solving (ready for TLS)
// max_difficulty: refuse to solve if server asks for more than this
td::actor::StartedTask<td::SocketPipe> solve_pow_client(td::SocketPipe pipe, td::int32 max_difficulty);

}  // namespace cocoon::pow
