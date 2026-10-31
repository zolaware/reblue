/**
 * @file    engine/d2anime/message_text.h
 * @brief   Writing text so the game's message renderer draws it as written.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <rex/types.h>

namespace bd::engine {

// Encodes text so the message renderer draws it as written.
std::u16string ToMessageText(std::u16string_view text);

// The text, cut short with "..." if it is wider than maxWidth.
std::string FitMessageText(std::string_view utf8, int fontWidth,
                           f32 maxWidth);

// Word-wraps the text to maxWidth with newlines.
std::string WrapMessageText(std::string_view utf8, int fontWidth,
                            f32 maxWidth);

// Wrapped lines, at most maxLines; the last ends in "..." if cut.
std::vector<std::string> WrapMessageLines(std::string_view utf8,
                                          int fontWidth, f32 maxWidth,
                                          size_t maxLines);

} // namespace bd::engine
