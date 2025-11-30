#include "cocoon/pow.h"
#include "td/actor/coro_task.h"
#include "td/net/utils.h"
#include "td/utils/as.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/tl_helpers.h"

namespace cocoon::pow {

PowChallenge PowChallenge::make_challenge(td::int32 difficulty_bits) {
  PowChallenge challenge;
  challenge.difficulty_bits = difficulty_bits;
  td::Random::secure_bytes(challenge.salt.raw, sizeof(challenge.salt.raw));
  return challenge;
}

td::int32 leading_zero_bits(const td::UInt256& hash) {
  // we won't support challenges with more than 64 difficulty bits anyway
  td::uint64 chunk = td::as<td::uint64>(hash.raw);
  return td::count_leading_zeroes64(chunk);
}

bool PowChallenge::verify_response(td::int64 nonce) const {
  // Compute hash = SHA256(salt || nonce)
  std::string data(24, '\0');
  std::memcpy(&data[0], salt.raw, 16);
  std::memcpy(&data[16], &nonce, 8);

  td::UInt256 hash;
  td::sha256(data, hash.as_mutable_slice());

  return leading_zero_bits(hash) >= static_cast<td::int32>(difficulty_bits);
}

td::optional<td::int64> PowSolver::solve(const PowChallenge& challenge) {
  // Try many iterations before yielding
  constexpr size_t MAX_ITERATIONS = 100000;

  std::string data(24, '\0');
  for (size_t i = 0; i < MAX_ITERATIONS; i++) {
    std::memcpy(&data[0], challenge.salt.raw, 16);
    std::memcpy(&data[16], &nonce_, 8);

    td::UInt256 hash;
    td::sha256(data, hash.as_mutable_slice());
    if (leading_zero_bits(hash) >= challenge.difficulty_bits) {
      return nonce_;
    }
    nonce_++;
  }

  return {};
}

// Server-side PoW verification actor
class PowVerifyServer : public td::TaskActor<td::SocketPipe> {
 public:
  PowVerifyServer(td::SocketPipe pipe, td::int32 difficulty_bits)
      : pipe_(std::move(pipe)), challenge_(PowChallenge::make_challenge(difficulty_bits)) {
  }

 private:
  void start_up() override {
    pipe_.subscribe();
  }

  static constexpr int WaitCode = 123;

  enum class State { SendChallenge, WaitResponse, Done };

  td::Status run() {
    if (state_ == State::SendChallenge) {
      auto serialized = td::serialize(challenge_);
      pipe_.output_buffer().append(serialized);
      state_ = State::WaitResponse;
    }

    if (state_ == State::WaitResponse) {
      // Need exactly 4 bytes for magic + 8 bytes for nonce = 12 bytes
      if (pipe_.input_buffer().size() < 12) {
        return td::Status::Error<WaitCode>();
      }

      // Read exactly 12 bytes for the response
      auto response_data = pipe_.input_buffer().cut_head(12).move_as_buffer_slice();
      PowResponse response;
      TRY_STATUS(td::unserialize(response, response_data.as_slice()));

      if (!challenge_.verify_response(response.nonce)) {
        return td::Status::Error("PoW verification failed");
      }

      state_ = State::Done;
    }

    return td::Status::OK();
  }

  td::Status do_loop() {
    TRY_STATUS(td::loop_read("pow-server", pipe_));
    auto status = run();
    if (status.code() != WaitCode) {
      return status;
    }
    TRY_STATUS(td::loop_write("pow-server", pipe_));
    return td::Status::OK();
  }

  td::actor::Task<Action> task_loop_once() override {
    co_await do_loop();
    co_return state_ == State::Done ? Action::Finish : Action::KeepRunning;
  }

  td::actor::Task<td::SocketPipe> finish(td::Status status) override {
    co_await std::move(status);
    auto socket = co_await std::move(pipe_).extract_fd();
    co_return td::make_socket_pipe(std::move(socket));
  }

  td::SocketPipe pipe_;
  PowChallenge challenge_;
  State state_{State::SendChallenge};
};

// Client-side PoW solver actor
class PowSolveClient : public td::TaskActor<td::SocketPipe> {
 public:
  explicit PowSolveClient(td::SocketPipe pipe, td::int32 max_difficulty)
      : pipe_(std::move(pipe)), max_difficulty_(max_difficulty) {
  }

 private:
  void start_up() override {
    pipe_.subscribe();
  }

  static constexpr int WaitCode = 123;

  enum class State { CheckMagic, Solve, SendResponse, Done };

  td::Status run() {
    if (state_ == State::CheckMagic) {
      // We have PoW magic, parse the full challenge
      // Challenge size: 4 bytes magic + 4 bytes difficulty + 16 bytes salt = 24 bytes
      if (pipe_.input_buffer().size() < 24) {
        return td::Status::Error<WaitCode>();
      }

      auto challenge_data = pipe_.input_buffer().cut_head(24).move_as_buffer_slice();
      TRY_STATUS(td::unserialize(challenge_, challenge_data.as_slice()));

      if (challenge_.difficulty_bits > max_difficulty_) {
        return td::Status::Error(PSLICE() << "Server requested PoW difficulty " << challenge_.difficulty_bits
                                          << " which exceeds max " << max_difficulty_);
      }

      LOG(INFO) << "Solving PoW: difficulty=" << challenge_.difficulty_bits;
      state_ = State::Solve;
    }

    if (state_ == State::Solve) {
      // Solve in tight loop until found
      auto solution = solver_.solve(challenge_);
      if (solution) {
        PowResponse response;
        response.nonce = solution.value();
        auto serialized = td::serialize(response);
        pipe_.output_buffer().append(serialized);
        state_ = State::SendResponse;
      } else {
        yield();
      }
    }

    if (state_ == State::SendResponse) {
      LOG(INFO) << "PowSolveClient: response sent";
      state_ = State::Done;
    }

    return td::Status::OK();
  }

  td::Status do_loop() {
    TRY_STATUS(td::loop_read("pow-client", pipe_));
    auto status = run();
    if (status.code() != WaitCode) {
      return status;
    }
    TRY_STATUS(td::loop_write("pow-client", pipe_));
    return td::Status::OK();
  }

  td::actor::Task<Action> task_loop_once() override {
    co_await do_loop();
    co_return state_ == State::Done ? Action::Finish : Action::KeepRunning;
  }

  td::actor::Task<td::SocketPipe> finish(td::Status status) override {
    co_await std::move(status);
    auto fd = co_await std::move(pipe_).extract_fd();
    co_return td::make_socket_pipe(std::move(fd));
  }

  td::SocketPipe pipe_;
  PowChallenge challenge_;
  PowSolver solver_;
  State state_{State::CheckMagic};
  bool skip_pow_{false};
  td::int32 max_difficulty_;
};

td::actor::StartedTask<td::SocketPipe> verify_pow_server(td::SocketPipe pipe, td::int32 difficulty_bits) {
  return td::spawn_task_actor<PowVerifyServer>("PowVerifyServer", std::move(pipe), difficulty_bits);
}

td::actor::StartedTask<td::SocketPipe> solve_pow_client(td::SocketPipe pipe, td::int32 max_difficulty) {
  return td::spawn_task_actor<PowSolveClient>("PowSolveClient", std::move(pipe), max_difficulty);
}

}  // namespace cocoon::pow
