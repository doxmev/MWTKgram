/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mwtk/mwtk_room_render.h"

#include <rpl/producer.h>

class QPainter;
class QWidget;

namespace Mwtk {

[[nodiscard]] RoomType CurrentRoom();
[[nodiscard]] bool PaintRoom(QPainter &p, QSize fill);

// Paints the room as one backdrop for the whole window, so the chat list
// and the chat area show the same continuous scene.
[[nodiscard]] bool PaintRoomInWindow(QPainter &p, not_null<QWidget*> widget);
[[nodiscard]] rpl::producer<> RoomRepaintRequests();
[[nodiscard]] QPointF RoomCameraShift();
[[nodiscard]] double RoomTime();
[[nodiscard]] RoomParams CurrentRoomParams();
bool PaintRoomVideo(QPainter &p, QSize fill);

} // namespace Mwtk
