/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Share {

// MWTK theme files (.mwtktheme): colors, scene, effects and icon.
// They can be posted anywhere; clicking one in MWTKgram applies it.
void ExportTheme(not_null<Window::SessionController*> controller);

// Encrypted sync file (.mwtk) sent to Saved Messages: options, playlists,
// notes, local names, phrases and hidden chats. Opening it on another
// device with the same account imports everything.
void SendSync(not_null<Window::SessionController*> controller);

// Called for opened documents: returns true when the file was an MWTK one.
bool HandleOpen(
	not_null<Window::SessionController*> controller,
	const QString &path);

} // namespace Mwtk::Share
