/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Hotkeys {

enum class Action {
	Playlists,
	HideChats,
	Settings,
	NextTrack,
	PreviousTrack,
	QualityCycle,
	Phrases,
	Sync,
	Count,
};

[[nodiscard]] QString ActionName(Action action);
[[nodiscard]] QKeySequence Get(Action action);
void Set(Action action, const QKeySequence &sequence);
[[nodiscard]] rpl::producer<> Changes();

// Installs the global key handler.
void Setup();

// Box that waits for the next key combination.
void ShowCaptureBox(
	not_null<Window::SessionController*> controller,
	Action action);

} // namespace Mwtk::Hotkeys
