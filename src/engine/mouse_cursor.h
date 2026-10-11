/**
 * @file    engine/mouse_cursor.h
 * @brief       The pointer, drawn with the game's own target cursor art.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

namespace bd::engine {

// Once per engine frame. Loads the texlist the first time a cursor is wanted and
// publishes whether the sprite is on screen, so the window thread can take the
// arrow away. Drawing happens per rendered frame in bdMouseCursorDrawHook.
void MouseCursorTick();

} // namespace bd::engine
