#if defined(_WIN32)

#include "connection.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>

struct WinConnection : public BaseConnection {
  HANDLE pipe{INVALID_HANDLE_VALUE};

  ~WinConnection() override { Close(); }

  bool Open() override {
    if (IsOpen())
      return true;

    char pipeName[64];
    for (int i = 0; i < 10; ++i) {
      snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\discord-ipc-%d", i);
      pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, 0, nullptr);
      if (pipe != INVALID_HANDLE_VALUE) {
        DWORD mode = PIPE_READMODE_BYTE;
        SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
        return true;
      }
    }
    return false;
  }

  bool Close() override {
    if (pipe != INVALID_HANDLE_VALUE) {
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
    }
    return true;
  }

  bool Write(const void *data, size_t length) override {
    if (!IsOpen())
      return false;

    DWORD written = 0;
    if (!WriteFile(pipe, data, static_cast<DWORD>(length), &written, nullptr)) {
      Close();
      return false;
    }
    return written == length;
  }

  bool Read(void *data, size_t length) override {
    if (!IsOpen())
      return false;

    DWORD bytesRead = 0;
    if (!ReadFile(pipe, data, static_cast<DWORD>(length), &bytesRead, nullptr)) {
      Close();
      return false;
    }
    return bytesRead == length;
  }

  bool HasData(size_t &available) override {
    available = 0;
    if (!IsOpen())
      return false;

    DWORD bytesAvail = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytesAvail, nullptr)) {
      Close();
      return false;
    }
    available = bytesAvail;
    return true;
  }

  bool IsOpen() const override { return pipe != INVALID_HANDLE_VALUE; }
};

BaseConnection *BaseConnection::Create() { return new WinConnection(); }

void BaseConnection::Destroy(BaseConnection *&conn) {
  delete conn;
  conn = nullptr;
}

#endif

