/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class QPainter;

namespace Mwtk::Appear {

void Register(not_null<HistoryItem*> item);

// Returns progress in [0, 1) while the item is still appearing, or -1.
[[nodiscard]] double Progress(not_null<const HistoryItem*> item);

// Prepares the painter for an appearing message, returns true if it did.
bool Apply(QPainter &p, double progress, bool out, int width);

} // namespace Mwtk::Appear
