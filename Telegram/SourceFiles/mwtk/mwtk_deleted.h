/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class PeerData;
class QPainter;

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Deleted {

// Remembers the messages before they are destroyed.
void Record(const std::vector<not_null<HistoryItem*>> &items, bool mine);

// Shows saved deleted messages of one chat, or of all chats if peer is null.
void ShowBox(
	not_null<Window::SessionController*> controller,
	PeerData *peer);

[[nodiscard]] bool HasAny(not_null<PeerData*> peer);

// Removes from the list the messages that should stay visible in the chat
// with a "deleted" mark, instead of disappearing.
void KeepInChat(std::vector<not_null<HistoryItem*>> &items);
[[nodiscard]] bool IsMarked(not_null<const HistoryItem*> item);
void PaintMarker(QPainter &p, int width, int height, bool out);

} // namespace Mwtk::Deleted
