/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Music {

// The music section: playlists as tiles, a playlist page with covers,
// search over public Telegram channels and the song that plays now.
void Show(not_null<Window::SessionController*> controller);

} // namespace Mwtk::Music
