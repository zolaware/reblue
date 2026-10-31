/**
 * @file    engine/d2anime/message_text.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/d2anime/message_text.h"

#include <algorithm>
#include <vector>

#include <rex/hook.h>
#include <rex/ppc/stack.h>
#include <rex/types.h>

#include "core/encoding.h"
#include "core/memory_helpers.h"

REX_IMPORT(__imp__bdTextCalcWidth, CalcTextWidth,
           f64(f64, u32, u32, u32, u32));

namespace bd::engine {

namespace {

constexpr char16_t kCodePrefix = u'#';

// Where MessageText_NextToken leaves its result in the tokenizer state.
constexpr u32 kNextTextOffset = 876;
constexpr u32 kTokenOffset = 880;
constexpr u32 kTokenArgOffset = 884;

// bdTextCalcWidth's default-font selector and whole-text length.
constexpr u32 kDefaultFont = 1;
constexpr u32 kWholeText = 0xFFFFFFFF;
constexpr u32 kOpaqueWhite = 0xFFFFFFFF;

constexpr std::u16string_view kEllipsis = u"...";

// Measures the text as displayed, since bdTextCalcWidth reads no codes.
f32 WidthOfMessageText(std::u16string_view text, int fontWidth) {
  std::vector<u16> bigEndian(text.size() + 1, 0);
  for (size_t i = 0; i < text.size(); ++i) {
    const char16_t shown = text[i] == u'\n' ? u' ' : text[i];
    bigEndian[i] = __builtin_bswap16(static_cast<u16>(shown));
  }

  rex::ppc::stack_guard guard;
  const u32 textAddr = rex::ppc::stack_push(
      bigEndian.data(), static_cast<u32>(bigEndian.size() * sizeof(u16)));
  return static_cast<f32>(CalcTextWidth(static_cast<f64>(fontWidth),
                                        kOpaqueWhite, textAddr, kDefaultFont,
                                        kWholeText));
}

bool IsEscapedByPrefix(char16_t c) {
  return c == u'#' || c == u'$' || c == u'>';
}

} // namespace

std::u16string ToMessageText(std::u16string_view text) {
  std::u16string out;
  out.reserve(text.size());
  for (char16_t c : text) {
    if (c == u'\n') {
      out += u' ';
    } else if (c == u'"') {
      out += u"\"\"";
    } else if (IsEscapedByPrefix(c)) {
      out += kCodePrefix;
      out += c;
    } else {
      out += c;
    }
  }
  return out;
}

// The longest prefix that still fits once "..." follows it, found by halving.
std::string FitMessageText(std::string_view utf8, int fontWidth,
                           f32 maxWidth) {
  const std::u16string text = bd::Utf8ToU16(utf8);
  if (WidthOfMessageText(text, fontWidth) <= maxWidth)
    return std::string(utf8);

  const auto shortened = [&](size_t length) {
    std::u16string cut = text.substr(0, length);
    while (!cut.empty() && cut.back() == u' ')
      cut.pop_back();
    return cut + std::u16string(kEllipsis);
  };
  size_t fits = 0;
  size_t tooLong = text.size();
  while (tooLong - fits > 1) {
    const size_t middle = (fits + tooLong) / 2;
    if (WidthOfMessageText(shortened(middle), fontWidth) <= maxWidth)
      fits = middle;
    else
      tooLong = middle;
  }
  return bd::U16ToUtf8(shortened(fits));
}

namespace {

// How many leading characters of 'word' fit in 'maxWidth', at least one.
size_t FittingPrefix(std::u16string_view word, int fontWidth, f32 maxWidth) {
  size_t fits = 1;
  size_t tooLong = word.size() + 1;
  while (tooLong - fits > 1) {
    const size_t middle = (fits + tooLong) / 2;
    if (WidthOfMessageText(word.substr(0, middle), fontWidth) <= maxWidth)
      fits = middle;
    else
      tooLong = middle;
  }
  return fits;
}

// Appends one paragraph, already free of newlines, wrapped to 'maxWidth'.
void WrapParagraph(std::u16string_view paragraph, int fontWidth, f32 maxWidth,
                   std::u16string &out) {
  std::u16string line;
  size_t start = 0;
  while (start <= paragraph.size()) {
    size_t end = paragraph.find(u' ', start);
    if (end == std::u16string_view::npos)
      end = paragraph.size();
    std::u16string_view word = paragraph.substr(start, end - start);
    start = end + 1;

    const std::u16string joined =
        line.empty() ? std::u16string(word) : line + u' ' + std::u16string(word);
    if (WidthOfMessageText(joined, fontWidth) <= maxWidth) {
      line = joined;
      continue;
    }
    if (!line.empty()) {
      out += line + u'\n';
      line.clear();
    }
    while (WidthOfMessageText(word, fontWidth) > maxWidth) {
      const size_t cut = FittingPrefix(word, fontWidth, maxWidth);
      out += std::u16string(word.substr(0, cut)) + u'\n';
      word.remove_prefix(cut);
    }
    line = std::u16string(word);
  }
  out += line;
}

} // namespace

std::string WrapMessageText(std::string_view utf8, int fontWidth,
                            f32 maxWidth) {
  const std::u16string text = bd::Utf8ToU16(utf8);
  std::u16string out;
  size_t start = 0;
  while (true) {
    const size_t end = text.find(u'\n', start);
    const std::u16string_view paragraph =
        std::u16string_view(text).substr(start, end == std::u16string::npos
                                                     ? std::u16string::npos
                                                     : end - start);
    WrapParagraph(paragraph, fontWidth, maxWidth, out);
    if (end == std::u16string::npos)
      break;
    out += u'\n';
    start = end + 1;
  }
  return bd::U16ToUtf8(out);
}

std::vector<std::string> WrapMessageLines(std::string_view utf8,
                                          int fontWidth, f32 maxWidth,
                                          size_t maxLines) {
  std::vector<std::string> lines;
  const std::string wrapped = WrapMessageText(utf8, fontWidth, maxWidth);
  size_t start = 0;
  while (start <= wrapped.size()) {
    size_t end = wrapped.find('\n', start);
    if (end == std::string::npos)
      end = wrapped.size();
    if (lines.size() + 1 == maxLines) {
      std::string rest = wrapped.substr(start);
      std::replace(rest.begin(), rest.end(), '\n', ' ');
      lines.push_back(FitMessageText(rest, fontWidth, maxWidth));
      break;
    }
    lines.push_back(wrapped.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

} // namespace bd::engine

// Reads '##', '#$' and '#>' as the literal second character.
REX_EXTERN(__imp__MessageText_NextToken);
REX_HOOK_RAW(MessageText_NextToken) {
  using namespace bd::engine;

  const u32 state = ctx.r3.u32;
  const u32 text = ctx.r4.u32;
  const char16_t first = bd::mem::load<u16>(text);
  const char16_t second = bd::mem::load<u16>(text + 2);
  if (first != kCodePrefix || !IsEscapedByPrefix(second)) {
    __imp__MessageText_NextToken(ctx, base);
    return;
  }

  const u32 next = text + 2 * sizeof(char16_t);
  bd::mem::store<u32>(state + kNextTextOffset, next);
  bd::mem::store<u32>(state + kTokenOffset, second);
  bd::mem::store<u32>(state + kTokenArgOffset, 0);
  ctx.r3.u64 = next;
}
