#include "rpc_connection.h"

#include <format>
#include <vector>

RpcConnection *RpcConnection::Create(const char *appId) {
  return new RpcConnection(appId ? appId : "");
}

void RpcConnection::Destroy(RpcConnection *&conn) {
  delete conn;
  conn = nullptr;
}

RpcConnection::RpcConnection(const char *appId)
    : appId_(appId ? appId : ""), connection_(BaseConnection::Create()) {}

RpcConnection::~RpcConnection() {
  Close();
  BaseConnection::Destroy(connection_);
}

bool RpcConnection::Open() {
  if (state_ != State::Disconnected)
    return true;

  if (!connection_->Open())
    return false;

  std::string handshake =
      std::format("{{\"v\":1,\"client_id\":\"{}\"}}", appId_);
  if (!WriteFrame(RpcOpcode::Handshake, handshake.data(), handshake.size())) {
    connection_->Close();
    return false;
  }

  state_ = State::SentHandshake;
  return true;
}

void RpcConnection::Close() {
  if (connection_)
    connection_->Close();
  state_ = State::Disconnected;
}

bool RpcConnection::IsOpen() const {
  return connection_ && connection_->IsOpen();
}

bool RpcConnection::WriteFrame(RpcOpcode opcode, const void *data,
                               size_t length) {
  if (!connection_ || !connection_->IsOpen())
    return false;

  RpcMessageHeader header{
      .opcode = static_cast<uint32_t>(opcode),
      .length = static_cast<uint32_t>(length),
  };

  if (!connection_->Write(&header, sizeof(header)))
    return false;

  if (length > 0 && data) {
    if (!connection_->Write(data, length))
      return false;
  }

  return true;
}

bool RpcConnection::ReadFrame(RpcOpcode &opcode, std::string &payload) {
  if (!connection_ || !connection_->IsOpen())
    return false;

  size_t available = 0;
  if (!connection_->HasData(available) || available < sizeof(RpcMessageHeader))
    return false;

  RpcMessageHeader header{};
  if (!connection_->Read(&header, sizeof(header))) {
    Close();
    return false;
  }

  opcode = static_cast<RpcOpcode>(header.opcode);
  payload.clear();
  if (header.length > 0) {
    payload.resize(header.length);
    if (!connection_->Read(payload.data(), header.length)) {
      Close();
      return false;
    }
  }

  return true;
}

