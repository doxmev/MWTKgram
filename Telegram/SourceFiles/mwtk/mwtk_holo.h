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

} // namespace Mwtk::Holo
