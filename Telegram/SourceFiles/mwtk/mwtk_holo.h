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

// Turns the profile into a glass hologram card that can be turned in 3D
// by holding the right mouse button. `head` is the top bar of the profile
// (it stays dense), `userpic` floats above the card; it may be null.
void AttachWindow(not_null<QWidget*> head, QWidget *userpic = nullptr);

} // namespace Mwtk::Holo
