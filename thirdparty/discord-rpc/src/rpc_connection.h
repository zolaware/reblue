#pragma once

#include "connection.h"

#include <cstdint>
#include <string>

enum class RpcOpcode : uint32_t {
  Handshake = 0,
  Frame = 1,
  Close = 2,
  Ping = 3,
  Pong = 4,
};

struct RpcMessageHeader {
  uint32_t opcode;
  uint32_t length;
};

class RpcConnection {
public:
  enum class State {
    Disconnected,
    SentHandshake,
    Connected,
  };

  static RpcConnection *Create(const char *appId);
  static void Destroy(RpcConnection *&conn);

  ~RpcConnection();

  bool Open();
  void Close();
  bool IsOpen() const;

  State GetState() const { return state_; }
  void SetState(State s) { state_ = s; }

  bool WriteFrame(RpcOpcode opcode, const void *data, size_t length);
  bool ReadFrame(RpcOpcode &opcode, std::string &payload);

private:
  explicit RpcConnection(const char *appId);

  std::string appId_;
  BaseConnection *connection_{nullptr};
  State state_{State::Disconnected};
  uint32_t nextNonce_{1};
};

