/**
 * @file    platform/http.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/http.h"

// First so every header below it finds Windows.h already included, lean.
#if defined(_WIN32)
#include "core/windows_lean.h"

#include <winhttp.h>
#elif defined(REBLUE_HAVE_CURL)
#include <curl/curl.h>
#include <dlfcn.h>
#endif

#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>

#include <rex/string/utf8.h>

#include "core/build_info.h"

namespace bd::platform {
namespace {

constexpr u32 kTimeoutMs = 30000;
constexpr size_t kReadChunk = 64 * 1024;

using BodySink = std::function<void(const char *, size_t)>;

#if defined(_WIN32)

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h)
      WinHttpCloseHandle(h);
  }
};

HTTPResult Perform(const std::string &url, const BodySink &sink,
                   const DownloadProgress &progress) {
  HTTPResult out;
  const std::wstring wide = rex::ToWideString(url);

  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
    out.error = "malformed URL: " + url;
    return out;
  }
  if (parts.lpszHostName == nullptr || parts.dwHostNameLength == 0) {
    out.error = "no host in " + url;
    return out;
  }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path;
  if (parts.lpszUrlPath != nullptr)
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.lpszExtraInfo != nullptr)
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  if (path.empty())
    path = L"/";

  const std::wstring agent = rex::ToWideString("reblue/" REBLUE_VERSION_STRING);
  Handle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                             0)};
  if (!session.h) {
    out.error = "WinHttpOpen failed";
    return out;
  }
  WinHttpSetTimeouts(session.h, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

  Handle connection{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
  if (!connection.h) {
    out.error = "cannot reach " + rex::ToUTF8(host);
    return out;
  }

  const DWORD flags =
      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  Handle request{WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr,
                                    WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
  if (!request.h) {
    out.error = "WinHttpOpenRequest failed";
    return out;
  }

  if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    out.error = "no reply from " + rex::ToUTF8(host);
    return out;
  }

  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(
          request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
          WINHTTP_NO_HEADER_INDEX)) {
    out.error = "reply carried no status";
    return out;
  }
  out.status = static_cast<int>(status);
  out.ok = true;

  u64 total = 0;
  DWORD length = 0;
  size = sizeof(length);
  if (WinHttpQueryHeaders(
          request.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
          WINHTTP_NO_HEADER_INDEX)) {
    total = length;
  }

  std::string chunk(kReadChunk, '\0');
  u64 done = 0;
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.h, chunk.data(),
                         static_cast<DWORD>(chunk.size()), &read)) {
      out.ok = false;
      out.error = "read failed after " + std::to_string(done) + " bytes";
      return out;
    }
    if (read == 0)
      break;
    sink(chunk.data(), read);
    done += read;
    if (progress)
      progress(done, total);
  }
  return out;
}

#elif defined(REBLUE_HAVE_CURL)

// A transfer moving fewer bytes than this for this long is dead. There is no
// cap on total time: a content pack takes minutes.
constexpr long kStallBytesPerSec = 512;
constexpr long kStallSeconds = 30;

// Loaded, not linked: a machine without libcurl loses the update check rather
// than the ability to start.
struct Curl {
  CURLcode (*global_init)(long);
  CURL *(*easy_init)();
  CURLcode (*easy_setopt)(CURL *, CURLoption, ...);
  CURLcode (*easy_perform)(CURL *);
  CURLcode (*easy_getinfo)(CURL *, CURLINFO, ...);
  void (*easy_cleanup)(CURL *);
  const char *(*easy_strerror)(CURLcode);
};

const Curl *LoadCurl(std::string &error) {
  static Curl curl{};
  static bool loaded = false;
  static std::string load_error;
  static std::once_flag once;

  std::call_once(once, [] {
    static constexpr const char *kNames[] = {
#if defined(__APPLE__)
        "libcurl.4.dylib",
        "libcurl.dylib",
#else
        "libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4",
#endif
    };
    void *lib = nullptr;
    for (const char *name : kNames) {
      lib = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
      if (lib != nullptr)
        break;
    }
    if (lib == nullptr) {
      const char *why = dlerror();
      load_error = std::string("libcurl could not be loaded: ") +
                   (why != nullptr ? why : "not installed");
      return;
    }

    const auto sym = [lib](const char *name) { return dlsym(lib, name); };
    curl.global_init =
        reinterpret_cast<CURLcode (*)(long)>(sym("curl_global_init"));
    curl.easy_init = reinterpret_cast<CURL *(*)()>(sym("curl_easy_init"));
    curl.easy_setopt = reinterpret_cast<CURLcode (*)(CURL *, CURLoption, ...)>(
        sym("curl_easy_setopt"));
    curl.easy_perform =
        reinterpret_cast<CURLcode (*)(CURL *)>(sym("curl_easy_perform"));
    curl.easy_getinfo = reinterpret_cast<CURLcode (*)(CURL *, CURLINFO, ...)>(
        sym("curl_easy_getinfo"));
    curl.easy_cleanup =
        reinterpret_cast<void (*)(CURL *)>(sym("curl_easy_cleanup"));
    curl.easy_strerror =
        reinterpret_cast<const char *(*)(CURLcode)>(sym("curl_easy_strerror"));

    loaded = curl.global_init != nullptr && curl.easy_init != nullptr &&
             curl.easy_setopt != nullptr && curl.easy_perform != nullptr &&
             curl.easy_getinfo != nullptr && curl.easy_cleanup != nullptr &&
             curl.easy_strerror != nullptr;
    if (!loaded) {
      load_error = "libcurl is missing entry points this build needs";
      return;
    }
    curl.global_init(CURL_GLOBAL_DEFAULT);
  });

  if (!loaded)
    error = load_error;
  return loaded ? &curl : nullptr;
}

size_t WriteThunk(char *data, size_t size, size_t count, void *user) {
  const auto *sink = static_cast<const BodySink *>(user);
  (*sink)(data, size *count);
  return size * count;
}

int ProgressThunk(void *user, curl_off_t total, curl_off_t done, curl_off_t,
                  curl_off_t) {
  const auto *progress = static_cast<const DownloadProgress *>(user);
  if (*progress)
    (*progress)(static_cast<u64>(done), static_cast<u64>(total));
  return 0;
}

HTTPResult Perform(const std::string &url, const BodySink &sink,
                   const DownloadProgress &progress) {
  HTTPResult out;
  const Curl *api = LoadCurl(out.error);
  if (api == nullptr)
    return out;

  CURL *curl = api->easy_init();
  if (curl == nullptr) {
    out.error = "curl_easy_init failed";
    return out;
  }

  char message[CURL_ERROR_SIZE] = {};
  api->easy_setopt(curl, CURLOPT_URL, url.c_str());
  api->easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  api->easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(kTimeoutMs));
  api->easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kStallBytesPerSec);
  api->easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kStallSeconds);
  api->easy_setopt(curl, CURLOPT_USERAGENT, "reblue/" REBLUE_VERSION_STRING);
  api->easy_setopt(curl, CURLOPT_ERRORBUFFER, message);
  api->easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteThunk);
  api->easy_setopt(curl, CURLOPT_WRITEDATA,
                   const_cast<void *>(static_cast<const void *>(&sink)));
  if (progress) {
    api->easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    api->easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressThunk);
    api->easy_setopt(curl, CURLOPT_XFERINFODATA,
                     const_cast<void *>(static_cast<const void *>(&progress)));
  }

  const CURLcode result = api->easy_perform(curl);
  if (result != CURLE_OK) {
    out.error = message[0] != '\0' ? message : api->easy_strerror(result);
    api->easy_cleanup(curl);
    return out;
  }

  long status = 0;
  api->easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  out.status = static_cast<int>(status);
  out.ok = true;
  api->easy_cleanup(curl);
  return out;
}

#else

HTTPResult Perform(const std::string &, const BodySink &,
                   const DownloadProgress &) {
  HTTPResult out;
  out.error = "this build has no HTTP client (built without the libcurl "
              "headers)";
  return out;
}

#endif

} // namespace

HTTPResult HTTP::Get(const std::string &url, std::string &body) {
  std::string buffer;
  const BodySink sink = [&buffer](const char *data, size_t n) {
    buffer.append(data, n);
  };
  HTTPResult out = Perform(url, sink, nullptr);
  if (out.Succeeded())
    body = std::move(buffer);
  return out;
}

HTTPResult HTTP::Download(const std::string &url,
                          const std::filesystem::path &dest,
                          const DownloadProgress &progress) {
  HTTPResult out;
  std::error_code ec;
  std::filesystem::create_directories(dest.parent_path(), ec);

  std::ofstream file(dest, std::ios::binary);
  if (!file) {
    out.error = "cannot write " + dest.string();
    return out;
  }

  const BodySink sink = [&file](const char *data, size_t n) {
    file.write(data, static_cast<std::streamsize>(n));
  };
  out = Perform(url, sink, progress);
  file.close();
  if (!file.good()) {
    out.ok = false;
    if (out.error.empty())
      out.error = "write failed for " + dest.string();
  }

  if (!out.Succeeded()) {
    std::filesystem::remove(dest, ec);
    if (out.error.empty())
      out.error = "HTTP " + std::to_string(out.status);
  }
  return out;
}

} // namespace bd::platform
