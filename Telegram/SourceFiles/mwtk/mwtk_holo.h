/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Mwtk::Holo {

// Puts a hologram layer (iridescent rim, scan lines and a light sweep)
// over a profile userpic. Follows the widget geometry, ignores the mouse.
void Attach(not_null<QWidget*> userpic);

// Turns the whole profile window into a hologram of the theme color:
// tint, moving scan lines, a light sweep, flicker and a shining frame.
// `inside` is any widget of the profile; the layer covers its section.
void AttachWindow(not_null<QWidget*> inside);

} // namespace Mwtk::Holo
