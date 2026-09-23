#pragma once

#include <cstddef>
#include <cstdint>

struct BaseConnection {
  static BaseConnection *Create();
  static void Destroy(BaseConnection *&conn);

  virtual ~BaseConnection() = default;
  virtual bool Open() = 0;
  virtual bool Close() = 0;
  virtual bool Write(const void *data, size_t length) = 0;
  virtual bool Read(void *data, size_t length) = 0;
  virtual bool HasData(size_t &available) = 0;
  virtual bool IsOpen() const = 0;
};

