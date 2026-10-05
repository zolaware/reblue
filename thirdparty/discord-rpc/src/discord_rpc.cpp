#include "discord_rpc.h"
#include "rpc_connection.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>

namespace {

std::string JsonEscape(const char *str) {
  if (!str)
    return "";
  std::string out;
  while (*str) {
    char c = *str++;
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
        out += buf;
      } else {
        out += c;
      }
      break;
    }
  }
  return out;
}

uint32_t GetCurrentPid() {
#if defined(_WIN32)
  return static_cast<uint32_t>(GetCurrentProcessId());
#else
  return static_cast<uint32_t>(getpid());
#endif
}

std::string g_appId;
DiscordEventHandlers g_handlers{};
RpcConnection *g_connection{nullptr};
std::mutex g_mutex;
uint32_t g_nonce{1};
bool g_initialized{false};
std::chrono::steady_clock::time_point g_lastConnectAttempt{};

void TryConnect() {
  if (!g_connection || g_connection->IsOpen())
    return;

  const auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::seconds>(now - g_lastConnectAttempt)
          .count() < 2) {
    return;
  }
  g_lastConnectAttempt = now;
  g_connection->Open();
}

} // namespace

void Discord_Initialize(const char *applicationId,
                        DiscordEventHandlers *handlers, int autoRegister,
                        const char *optionalSteamId) {
  (void)autoRegister;
  (void)optionalSteamId;

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized)
    return;

  g_appId = applicationId ? applicationId : "";
  if (handlers)
    g_handlers = *handlers;
  else
    std::memset(&g_handlers, 0, sizeof(g_handlers));

  g_connection = RpcConnection::Create(g_appId.c_str());
  g_initialized = true;
  TryConnect();
}

void Discord_Shutdown(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized)
    return;

  if (g_connection) {
    RpcConnection::Destroy(g_connection);
  }
  std::memset(&g_handlers, 0, sizeof(g_handlers));
  g_appId.clear();
  g_initialized = false;
}

void Discord_UpdateHandlers(DiscordEventHandlers *handlers) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (handlers)
    g_handlers = *handlers;
  else
    std::memset(&g_handlers, 0, sizeof(g_handlers));
}

void Discord_RunCallbacks(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized || !g_connection)
    return;

  if (!g_connection->IsOpen()) {
    TryConnect();
    return;
  }

  RpcOpcode opcode;
  std::string payload;
  while (g_connection->ReadFrame(opcode, payload)) {
    if (opcode == RpcOpcode::Close) {
      g_connection->Close();
      if (g_handlers.disconnected)
        g_handlers.disconnected(0, "RPC closed by Discord");
      break;
    }

    if (opcode == RpcOpcode::Frame) {
      if (g_connection->GetState() == RpcConnection::State::SentHandshake) {
        if (payload.find("\"evt\":\"READY\"") != std::string::npos ||
            payload.find("\"READY\"") != std::string::npos) {
          g_connection->SetState(RpcConnection::State::Connected);
          if (g_handlers.ready) {
            DiscordUser user{};
            user.userId = "";
            user.username = "Player";
            g_handlers.ready(&user);
          }
        }
      }
    }
  }
}

void Discord_UpdatePresence(const DiscordRichPresence *presence) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized || !g_connection)
    return;

  if (!g_connection->IsOpen()) {
    TryConnect();
    if (!g_connection->IsOpen())
      return;
  }

  if (!presence) {
    Discord_ClearPresence();
    return;
  }

  std::string json = std::format(
      "{{"
      "\"cmd\":\"SET_ACTIVITY\","
      "\"args\":{{"
      "\"pid\":{},"
      "\"activity\":{{",
      GetCurrentPid());

  bool hasPrev = false;

  if (presence->state && presence->state[0]) {
    json += std::format("\"state\":\"{}\"", JsonEscape(presence->state));
    hasPrev = true;
  }

  if (presence->details && presence->details[0]) {
    if (hasPrev)
      json += ",";
    json += std::format("\"details\":\"{}\"", JsonEscape(presence->details));
    hasPrev = true;
  }

  if (presence->startTimestamp > 0 || presence->endTimestamp > 0) {
    if (hasPrev)
      json += ",";
    json += "\"timestamps\":{";
    bool hasTs = false;
    if (presence->startTimestamp > 0) {
      json += std::format("\"start\":{}", presence->startTimestamp);
      hasTs = true;
    }
    if (presence->endTimestamp > 0) {
      if (hasTs)
        json += ",";
      json += std::format("\"end\":{}", presence->endTimestamp);
    }
    json += "}";
    hasPrev = true;
  }

  bool hasLarge = presence->largeImageKey && presence->largeImageKey[0];
  bool hasSmall = presence->smallImageKey && presence->smallImageKey[0];
  if (hasLarge || hasSmall) {
    if (hasPrev)
      json += ",";
    json += "\"assets\":{";
    bool hasAsset = false;
    if (hasLarge) {
      json += std::format("\"large_image\":\"{}\"",
                          JsonEscape(presence->largeImageKey));
      if (presence->largeImageText && presence->largeImageText[0]) {
        json += std::format(",\"large_text\":\"{}\"",
                            JsonEscape(presence->largeImageText));
      }
      hasAsset = true;
    }
    if (hasSmall) {
      if (hasAsset)
        json += ",";
      json += std::format("\"small_image\":\"{}\"",
                          JsonEscape(presence->smallImageKey));
      if (presence->smallImageText && presence->smallImageText[0]) {
        json += std::format(",\"small_text\":\"{}\"",
                            JsonEscape(presence->smallImageText));
      }
    }
    json += "}";
    hasPrev = true;
  }

  if (presence->partySize > 0) {
    if (hasPrev)
      json += ",";
    json += "\"party\":{";
    bool hasParty = false;
    if (presence->partyId && presence->partyId[0]) {
      json += std::format("\"id\":\"{}\"", JsonEscape(presence->partyId));
      hasParty = true;
    }
    if (presence->partyMax > 0) {
      if (hasParty)
        json += ",";
      json += std::format("\"size\":[{},{}]", presence->partySize,
                          presence->partyMax);
    }
    json += "}";
    hasPrev = true;
  }

  if (presence->instance) {
    if (hasPrev)
      json += ",";
    json += "\"instance\":true";
  }

  json += std::format(
      "}}}},"
      "\"nonce\":\"{}\"}}",
      g_nonce++);

  g_connection->WriteFrame(RpcOpcode::Frame, json.data(), json.size());
}

void Discord_ClearPresence(void) {
  if (!g_initialized || !g_connection || !g_connection->IsOpen())
    return;

  std::string json = std::format(
      "{{"
      "\"cmd\":\"SET_ACTIVITY\","
      "\"args\":{{"
      "\"pid\":{},"
      "\"activity\":null"
      "}},"
      "\"nonce\":\"{}\"}}",
      GetCurrentPid(), g_nonce++);

  g_connection->WriteFrame(RpcOpcode::Frame, json.data(), json.size());
}

void Discord_Respond(const char *userId, int reply) {
  (void)userId;
  (void)reply;
}

