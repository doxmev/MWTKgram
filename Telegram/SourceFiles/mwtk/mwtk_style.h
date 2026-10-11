/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;
class QRect;
class QColor;

namespace Mwtk {

void SetupChatStyle();

[[nodiscard]] bool PaintDialogCard(
	QPainter &p,
	QRect geometry,
	const QColor &background,
	bool active,
	bool selected);

} // namespace Mwtk
