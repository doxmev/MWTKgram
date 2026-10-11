/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QPointF>
#include <QtCore/QSize>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtCore/QString>

#include <vector>

class QPainter;

namespace Mwtk {

enum class RoomType {
	None = 0,
	Forest = 1,
	Neon = 2,
};

struct RoomParams {
	RoomType type = RoomType::None;
	double yaw = 0.;
	double pitch = 0.;
	double fov = 75.;
	double ratio = 1.;
	double scale = 1.;
	int trees = 460;
	QColor accent;

	friend inline bool operator==(
			const RoomParams &a,
			const RoomParams &b) {
		return (a.type == b.type)
			&& (a.yaw == b.yaw)
			&& (a.pitch == b.pitch)
			&& (a.fov == b.fov)
			&& (a.ratio == b.ratio)
			&& (a.scale == b.scale)
			&& (a.trees == b.trees)
			&& (a.accent == b.accent);
	}
};

struct RoomLayer {
	QImage image;
	double depth = 0.;
};

struct RoomFrame {
	std::vector<RoomLayer> layers;
	QSize size;
	int margin = 0;
	double focal = 1.;
	QColor fog;
	QImage vignette;
};

[[nodiscard]] RoomFrame RenderRoom(const RoomParams &params, QSize size);

void PaintRoomFrame(
	QPainter &p,
	const RoomFrame &frame,
	QSize fill,
	QPointF cameraShift,
	double time,
	bool motion);

[[nodiscard]] QString RoomName(RoomType type);

[[nodiscard]] QImage RenderBranch(QSize size, double ratio);

} // namespace Mwtk
