/**
 * @file    core/encoding.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "core/encoding.h"
#if defined(_WIN32)
#include "core/windows_lean.h"
namespace bd {
std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring o((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n);
  return o;
}
std::string WideToUtf8(std::wstring_view s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string o((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n, nullptr,
                      nullptr);
  return o;
}
std::string SjisToUtf8(std::string_view s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(932, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0)
    return std::string(s);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(932, 0, s.data(), (int)s.size(), w.data(), n);
  return WideToUtf8(w);
}
std::string Utf8ToSjis(std::string_view s) {
  if (s.empty())
    return {};
  std::wstring w = Utf8ToWide(s);
  int n = WideCharToMultiByte(932, 0, w.data(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  if (n <= 0)
    return std::string(s);
  std::string o((size_t)n, '\0');
  WideCharToMultiByte(932, 0, w.data(), (int)w.size(), o.data(), n, nullptr,
                      nullptr);
  return o;
}
std::string U16ToUtf8(std::u16string_view s) {
  if (s.empty())
    return {};
  static_assert(sizeof(wchar_t) == sizeof(char16_t));
  return WideToUtf8(
      std::wstring_view(reinterpret_cast<const wchar_t *>(s.data()), s.size()));
}
std::u16string Utf8ToU16(std::string_view s) {
  static_assert(sizeof(wchar_t) == sizeof(char16_t));
  std::wstring w = Utf8ToWide(s);
  return std::u16string(reinterpret_cast<const char16_t *>(w.data()), w.size());
}
} // namespace bd

#else // !defined(_WIN32)

#include <iconv.h>
#include <rex/types.h>
#include <vector>

namespace bd {
namespace {

void AppendUtf8(std::string &out, u32 cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

u32 NextUtf8(std::string_view s, size_t &i) {
  u8 c = static_cast<u8>(s[i++]);
  if (c < 0x80)
    return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
  u32 cp = c & (0x3Fu >> extra);
  for (int k = 0; k < extra && i < s.size(); ++k)
    cp = (cp << 6) | (static_cast<u8>(s[i++]) & 0x3F);
  return cp;
}

std::string Convert(std::string_view s, const char *to, const char *from) {
  if (s.empty())
    return {};
  iconv_t cd = iconv_open(to, from);
  if (cd == reinterpret_cast<iconv_t>(-1))
    return std::string(s);
  std::string in(s);
  std::vector<char> out(in.size() * 4 + 4);
  char *inbuf = in.data();
  size_t inleft = in.size();
  char *outbuf = out.data();
  size_t outleft = out.size();
  while (inleft) {
    if (iconv(cd, &inbuf, &inleft, &outbuf, &outleft) ==
        static_cast<size_t>(-1)) {
      // Skip the offending byte so one bad sequence does not lose the string.
      if (!outleft || !inleft)
        break;
      ++inbuf;
      --inleft;
    }
  }
  iconv_close(cd);
  return std::string(out.data(), static_cast<size_t>(outbuf - out.data()));
}

} // namespace

// wchar_t is 32-bit on Linux, so wide strings hold raw code points.
std::wstring Utf8ToWide(std::string_view s) {
  std::wstring o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size();)
    o.push_back(static_cast<wchar_t>(NextUtf8(s, i)));
  return o;
}

std::string WideToUtf8(std::wstring_view s) {
  std::string o;
  o.reserve(s.size());
  for (wchar_t c : s)
    AppendUtf8(o, static_cast<u32>(c));
  return o;
}

std::string SjisToUtf8(std::string_view s) {
  return Convert(s, "UTF-8", "CP932");
}

std::string Utf8ToSjis(std::string_view s) {
  return Convert(s, "CP932", "UTF-8");
}

std::u16string Utf8ToU16(std::string_view s) {
  std::u16string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    u32 cp = NextUtf8(s, i);
    if (cp >= 0x10000) {
      cp -= 0x10000;
      o.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      o.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      o.push_back(static_cast<char16_t>(cp));
    }
  }
  return o;
}

std::string U16ToUtf8(std::u16string_view s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    u32 cp = static_cast<u16>(s[i++]);
    if (cp >= 0xD800 && cp <= 0xDBFF && i < s.size()) {
      u32 lo = static_cast<u16>(s[i++]);
      cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
    }
    AppendUtf8(o, cp);
  }
  return o;
}

} // namespace bd
#endif
