/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class DocumentData;

namespace Mwtk::Lyrics {

// One timed line of a song. Times are in milliseconds.
struct Line {
	crl::time at = 0;
	crl::time end = 0;
	QString text;
	// Word start times from enhanced LRC, empty when only lines are timed.
	std::vector<crl::time> wordTimes;
};

// Parses LRC text: [mm:ss.xx] line tags, [offset:+/-ms] and enhanced
// <mm:ss.xx> word tags. Lines come out sorted with filled end times.
[[nodiscard]] std::vector<Line> ParseLrc(const QString &text);

// Builds LRC text from lines (for the sync editor).
[[nodiscard]] QString SerializeLrc(const std::vector<Line> &lines);

inline constexpr auto kLayoutCount = 3;
inline constexpr auto kVanishCount = 3;
inline constexpr auto kStyleCount = 3;
[[nodiscard]] QString LayoutName(int index);
[[nodiscard]] QString VanishName(int index);
[[nodiscard]] QString StyleName(int index);
[[nodiscard]] QString AppearName(int index);
[[nodiscard]] QString DriftName(int index);

// Creates the flying lyrics layer over the main window body.
void Setup(not_null<Ui::RpWidget*> body);

// "Hide chats" mode toggle for the context menu of a song.
[[nodiscard]] bool HideChatsEnabled();
void ToggleHideChats();

// Per-track timing shift and the sync editor.
void ShowOffsetBox(not_null<Window::SessionController*> controller);
void ShowSyncEditor(not_null<Window::SessionController*> controller);
void ShowSearchBox(not_null<Window::SessionController*> controller);

// Lyrics saved on this computer for the song in this file.
[[nodiscard]] bool HasLyrics(DocumentData *document);

// The whole text of the song that plays, like in VK: the current line
// glows and follows the music, a click on a line seeks there.
void ShowLyricsList(not_null<Window::SessionController*> controller);

// Song context menu: hide chats, timing shift and the sync editor.
void FillSongMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller);

} // namespace Mwtk::Lyrics
