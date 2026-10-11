/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mwtk/mwtk_room_render.h"

#include <rpl/producer.h>

class QPainter;

namespace Mwtk {

[[nodiscard]] RoomType CurrentRoom();
[[nodiscard]] bool PaintRoom(QPainter &p, QSize fill);
[[nodiscard]] rpl::producer<> RoomRepaintRequests();
[[nodiscard]] QPointF RoomCameraShift();
[[nodiscard]] double RoomTime();
[[nodiscard]] RoomParams CurrentRoomParams();
bool PaintRoomVideo(QPainter &p, QSize fill);

} // namespace Mwtk
