/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;
class QRect;
class QColor;
class PeerData;
class QWidget;

namespace Ui {
struct PeerUserpicView;
} // namespace Ui

namespace Mwtk {

void SetupChatStyle();

[[nodiscard]] bool PaintDialogCard(
	QPainter &p,
	QRect geometry,
	const QColor &background,
	bool active,
	bool selected,
	PeerData *peer = nullptr);

void PaintAvatarRing(QPainter &p, QRect rect, PeerData *peer);

// Online marks in the chat list: the dot may be hidden, the userpic may
// become a rounded tile and the dot may slowly pulse.
[[nodiscard]] bool OnlineDotShown();
void PaintOnlineExtras(
	QPainter &p,
	QRect userpic,
	PeerData *peer,
	Ui::PeerUserpicView &view,
	const QColor &background);

// Repaints only the pulsing dots painted into this widget, ~15 fps.
void SchedulePulseRepaint(not_null<QWidget*> widget);

} // namespace Mwtk
