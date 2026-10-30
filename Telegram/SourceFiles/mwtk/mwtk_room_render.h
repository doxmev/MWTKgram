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
	Custom = 3,
	Photo = 4,
	Video = 5,
};

enum class SceneBase {
	Forest = 0,
	Neon = 1,
	Space = 2,
	City = 3,
	Underwater = 4,
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
	SceneBase base = SceneBase::Forest;
	int timeOfDay = 0;
	int density = 100;
	int fog = 50;
	QString notes;
	QString photo;
	int dim = 30;
	bool night = false;

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
			&& (a.accent == b.accent)
			&& (a.base == b.base)
			&& (a.timeOfDay == b.timeOfDay)
			&& (a.density == b.density)
			&& (a.fog == b.fog)
			&& (a.notes == b.notes)
			&& (a.photo == b.photo)
			&& (a.dim == b.dim)
			&& (a.night == b.night);
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
	bool video = false;
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

[[nodiscard]] bool PhotoAvailable(const QString &path);

} // namespace Mwtk
