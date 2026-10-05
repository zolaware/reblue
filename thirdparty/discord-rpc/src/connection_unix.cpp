#if !defined(_WIN32)

#include "connection.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct UnixConnection : public BaseConnection {
  int sock{-1};

  ~UnixConnection() override { Close(); }

  bool Open() override {
    if (IsOpen())
      return true;

    const char *tempPaths[] = {
        getenv("XDG_RUNTIME_DIR"),
        getenv("TMPDIR"),
        getenv("TMP"),
        getenv("TEMP"),
        "/tmp",
    };

    for (const char *base : tempPaths) {
      if (!base || !*base)
        continue;

      for (int i = 0; i < 10; ++i) {
        char path[256];
        snprintf(path, sizeof(path), "%s/discord-ipc-%d", base, i);

        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0)
          continue;

        fcntl(sock, F_SETFD, FD_CLOEXEC);

        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        if (connect(sock, reinterpret_cast<struct sockaddr *>(&addr),
                    sizeof(addr)) == 0) {
          return true;
        }

        close(sock);
        sock = -1;
      }
    }
    return false;
  }

  bool Close() override {
    if (sock >= 0) {
      close(sock);
      sock = -1;
    }
    return true;
  }

  bool Write(const void *data, size_t length) override {
    if (!IsOpen())
      return false;

    size_t sent = 0;
    while (sent < length) {
      ssize_t res = write(sock, static_cast<const char *>(data) + sent,
                          length - sent);
      if (res < 0) {
        if (errno == EINTR)
          continue;
        Close();
        return false;
      }
      sent += static_cast<size_t>(res);
    }
    return true;
  }

  bool Read(void *data, size_t length) override {
    if (!IsOpen())
      return false;

    size_t received = 0;
    while (received < length) {
      ssize_t res = read(sock, static_cast<char *>(data) + received,
                         length - received);
      if (res < 0) {
        if (errno == EINTR)
          continue;
        Close();
        return false;
      }
      if (res == 0) {
        Close();
        return false;
      }
      received += static_cast<size_t>(res);
    }
    return true;
  }

  bool HasData(size_t &available) override {
    available = 0;
    if (!IsOpen())
      return false;

    int bytes = 0;
    if (ioctl(sock, FIONREAD, &bytes) < 0) {
      Close();
      return false;
    }
    available = static_cast<size_t>(bytes);
    return true;
  }

  bool IsOpen() const override { return sock >= 0; }
};

BaseConnection *BaseConnection::Create() { return new UnixConnection(); }

void BaseConnection::Destroy(BaseConnection *&conn) {
  delete conn;
  conn = nullptr;
}

#endif

