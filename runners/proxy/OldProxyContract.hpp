#pragma once

#include "runners/BaseRunner.hpp"

namespace cocoon {

class ProxyRunner;

class OldProxyContract {
 public:
  struct ClosingState {
    enum Values : td::uint32 {
      None = 0,
      NotStarted = 1,
      StartedClients = 2,
      StartedWorkers = 3,
      Closing = 4,
      Closed = 5
    };
  };
  OldProxyContract(const block::StdAddress &sc_addr, td::int32 closing_state, td::int32 close_at,
                   std::string next_client, std::string next_worker, std::shared_ptr<RunnerConfig> config,
                   ProxyRunner *proxy_runner)
      : sc_addr_(sc_addr)
      , closing_state_(closing_state)
      , close_at_(close_at)
      , next_client_(next_client)
      , next_worker_(next_worker)
      , config_(std::move(config))
      , proxy_runner_(proxy_runner) {
  }
  OldProxyContract(const cocoon_api::proxyDb_oldInstance &instance, ProxyRunner *proxy_runner);

  ton::tl_object_ptr<cocoon_api::proxyDb_oldInstance> serialize() const {
    return ton::create_tl_object<cocoon_api::proxyDb_oldInstance>(sc_addr_.rserialize(true), closing_state_, close_at_,
                                                                  next_client_, next_worker_,
                                                                  config_->root_contract_config->serialize());
  }

  bool is_finished() const {
    return closing_state_ == ClosingState::Closed;
  }

  bool ready_to_send_next_message() const {
    return !running_message_ &&
           (closing_state_ == ClosingState::NotStarted || closing_state_ == ClosingState::StartedClients ||
            closing_state_ == ClosingState::StartedWorkers ||
            (closing_state_ == ClosingState::Closing && close_at_ < (int)std::time(0)));
  }

  void message_sent_success();

  void send_next_message();

  void advance_state() {
    if (closing_state_ == ClosingState::StartedClients) {
      if (next_client_ == "") {
        closing_state_ = ClosingState::StartedWorkers;
      } else {
        return;
      }
    }
    if (closing_state_ == ClosingState::StartedWorkers) {
      if (next_worker_ == "") {
        closing_state_ = ClosingState::Closing;
      } else {
        return;
      }
    }
  }

  auto state() const {
    return (td::int32)closing_state_;
  }

  auto running_message() const {
    return running_message_;
  }

  void store_stats(td::StringBuilder &sb);
  void store_stats(SimpleJsonSerializer &jb);

  ClientCheckResult check();

 private:
  block::StdAddress sc_addr_;
  td::int32 closing_state_;
  td::int32 close_at_;
  bool running_message_{false};
  std::string next_client_;
  std::string next_worker_;
  std::shared_ptr<RunnerConfig> config_;
  ProxyRunner *proxy_runner_;
};

}  // namespace cocoon
