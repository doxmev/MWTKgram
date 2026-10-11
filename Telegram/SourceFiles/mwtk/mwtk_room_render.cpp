/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_room_render.h"

#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QImageReader>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPolygonF>
#include <QtGui/QRadialGradient>
#include <QtGui/QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <random>

namespace Mwtk {
namespace {

constexpr auto kPi = 3.14159265358979323846;
constexpr auto kEyeHeight = 1.6;
constexpr auto kNearPlane = 0.08;

[[nodiscard]] double Clamp(double value, double min, double max) {
	return std::max(min, std::min(max, value));
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double ratio) {
	const auto r = Clamp(ratio, 0., 1.);
	return QColor::fromRgbF(
		float(a.redF() * (1. - r) + b.redF() * r),
		float(a.greenF() * (1. - r) + b.greenF() * r),
		float(a.blueF() * (1. - r) + b.blueF() * r),
		float(a.alphaF() * (1. - r) + b.alphaF() * r));
}

[[nodiscard]] QColor WithAlpha(QColor color, double alpha) {
	color.setAlphaF(float(Clamp(alpha, 0., 1.)));
	return color;
}

struct Vec {
	double x = 0.;
	double y = 0.;
	double z = 0.;
};

class Camera final {
public:
	Camera(const RoomParams &params, QSize view, int margin)
	: _cyaw(std::cos(params.yaw * kPi / 180.))
	, _syaw(std::sin(params.yaw * kPi / 180.))
	, _cpitch(std::cos(params.pitch * kPi / 180.))
	, _spitch(std::sin(params.pitch * kPi / 180.))
	, _center(margin + view.width() / 2., margin + view.height() / 2.) {
		const auto half = Clamp(params.fov, 30., 120.) * kPi / 360.;
		_focal = (std::max(view.width(), 1) / 2.) / std::tan(half);
	}

	[[nodiscard]] Vec toCamera(Vec world) const {
		const auto dx = world.x;
		const auto dy = world.y - kEyeHeight;
		const auto dz = world.z;
		const auto x1 = dx * _cyaw - dz * _syaw;
		const auto z1 = dx * _syaw + dz * _cyaw;
		return {
			x1,
			dy * _cpitch - z1 * _spitch,
			dy * _spitch + z1 * _cpitch,
		};
	}
	[[nodiscard]] QPointF fromCamera(Vec camera) const {
		const auto z = std::max(camera.z, kNearPlane);
		return QPointF(
			_center.x() + camera.x * _focal / z,
			_center.y() - camera.y * _focal / z);
	}
	[[nodiscard]] QPointF project(Vec world, double *depth = nullptr) const {
		const auto camera = toCamera(world);
		if (depth) {
			*depth = camera.z;
		}
		return fromCamera(camera);
	}
	[[nodiscard]] bool clipLine(Vec &a, Vec &b) const {
		if (a.z < kNearPlane && b.z < kNearPlane) {
			return false;
		}
		const auto cut = [](Vec &from, const Vec &to) {
			const auto t = (kNearPlane - from.z) / (to.z - from.z);
			from = {
				from.x + (to.x - from.x) * t,
				from.y + (to.y - from.y) * t,
				kNearPlane,
			};
		};
		if (a.z < kNearPlane) {
			cut(a, b);
		} else if (b.z < kNearPlane) {
			cut(b, a);
		}
		return true;
	}
	[[nodiscard]] double focal() const {
		return _focal;
	}
	[[nodiscard]] QPointF center() const {
		return _center;
	}
	[[nodiscard]] double horizonY() const {
		return _center.y() + (_spitch / std::max(_cpitch, 0.01)) * _focal;
	}

private:
	double _cyaw = 1.;
	double _syaw = 0.;
	double _cpitch = 1.;
	double _spitch = 0.;
	QPointF _center;
	double _focal = 1.;

};

[[nodiscard]] double RenderRatio(const RoomParams &params) {
	return std::max(0.2, params.ratio * std::clamp(params.scale, 0.3, 1.));
}

struct Tree {
	double x = 0.;
	double z = 0.;
	double height = 10.;
	double width = 3.5;
	double shade = 0.;
	int tiers = 8;
	int note = -1;
	double noteHeight = 1.6;
	double noteTilt = 0.;
};

const char *kNotes[] = {
	"HELP ME",
	"HELP ME",
	"ПОМОГИ",
	"HELP",
	"НЕ ОБОРАЧИВАЙСЯ",
	"ОН РЯДОМ",
	"ГДЕ Я?",
	"ВЫХОДА НЕТ",
	"I SEE YOU",
	"RUN",
	"HELP ME",
	"НЕ ОСТАНАВЛИВАЙСЯ",
};

[[nodiscard]] const std::vector<Tree> &ForestTrees() {
	static const auto result = [] {
		auto trees = std::vector<Tree>();
		auto random = std::mt19937(1337);
		auto unit = std::uniform_real_distribution<double>(0., 1.);
		auto notes = 0;
		const double hero[][2] = {
			{ 4., 5.4 }, { 68., 6.2 }, { 140., 5.6 }, { 205., 6.4 }, { 290., 5.8 },
		};
		for (const auto &[degrees, radius] : hero) {
			const auto angle = degrees * kPi / 180.;
			auto tree = Tree();
			tree.x = std::sin(angle) * radius;
			tree.z = std::cos(angle) * radius;
			tree.height = 12. + unit(random) * 4.;
			tree.width = tree.height * 0.32;
			tree.shade = unit(random);
			tree.tiers = 9;
			tree.note = (notes++ == 0) ? 0 : (notes * 3) % int(std::size(kNotes));
			tree.noteHeight = 1.45;
			tree.noteTilt = (unit(random) - 0.5) * 10.;
			trees.push_back(tree);
		}
		while (trees.size() < 460) {
			const auto radius = 4.2 + std::sqrt(unit(random)) * 76.;
			const auto angle = unit(random) * 2. * kPi;
			auto tree = Tree();
			tree.x = std::sin(angle) * radius;
			tree.z = std::cos(angle) * radius;
			auto tooClose = false;
			for (const auto &other : trees) {
				const auto dx = other.x - tree.x;
				const auto dz = other.z - tree.z;
				if (dx * dx + dz * dz < 2.2 * 2.2) {
					tooClose = true;
					break;
				}
			}
			if (tooClose) {
				continue;
			}
			tree.height = 7. + unit(random) * 10.;
			tree.width = tree.height * (0.3 + 0.1 * unit(random));
			tree.shade = unit(random);
			tree.tiers = 7 + int(unit(random) * 4.);
			if (radius < 12. && unit(random) < 0.45) {
				tree.note = notes++ % int(std::size(kNotes));
				tree.noteHeight = 1.25 + unit(random) * 0.6;
				tree.noteTilt = (unit(random) - 0.5) * 14.;
			}
			trees.push_back(tree);
		}
		return trees;
	}();
	return result;
}

[[nodiscard]] std::vector<QString> ParseNotes(const QString &text) {
	auto result = std::vector<QString>();
	for (const auto &line : text.split(QChar('\n'))) {
		const auto trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			result.push_back(trimmed.left(48));
		}
		if (result.size() >= 40) {
			break;
		}
	}
	if (result.empty()) {
		for (const auto note : kNotes) {
			result.push_back(QString::fromUtf8(note));
		}
	}
	return result;
}

[[nodiscard]] QImage NoteTexture(const QString &text) {
	static auto cache = QHash<QString, QImage>();
	if (const auto i = cache.constFind(text); i != cache.constEnd()) {
		return *i;
	}
	if (cache.size() > 64) {
		cache.clear();
	}
	auto &result = cache[text];
	const auto width = 320;
	const auto height = 230;
	result = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.setRenderHint(QPainter::Antialiasing);
	auto random = std::mt19937(77 + uint(qHash(text) % 1000));
	auto unit = std::uniform_real_distribution<double>(0., 1.);

	auto paper = QPainterPath();
	paper.moveTo(6, 10);
	for (auto i = 1; i <= 8; ++i) {
		paper.lineTo(6 + i * (width - 12) / 8., 6 + unit(random) * 8.);
	}
	paper.lineTo(width - 8, height - 10);
	for (auto i = 7; i >= 0; --i) {
		paper.lineTo(8 + i * (width - 16) / 8., height - 6 - unit(random) * 9.);
	}
	paper.closeSubpath();
	auto fill = QLinearGradient(0, 0, width, height);
	fill.setColorAt(0., QColor(222, 214, 192));
	fill.setColorAt(1., QColor(184, 174, 150));
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawPath(paper);
	for (auto i = 0; i != 5; ++i) {
		const auto c = QPointF(unit(random) * width, unit(random) * height);
		const auto r = 18. + unit(random) * 40.;
		auto stain = QRadialGradient(c, r);
		stain.setColorAt(0., QColor(120, 96, 60, 70));
		stain.setColorAt(1., QColor(120, 96, 60, 0));
		p.setBrush(stain);
		p.drawEllipse(c, r, r);
	}

	auto font = QFont();
	font.setFamilies({
		QStringLiteral("Segoe Print"),
		QStringLiteral("Ink Free"),
		QStringLiteral("Comic Sans MS"),
		QStringLiteral("Segoe UI"),
	});
	font.setBold(true);
	auto pixel = 78;
	font.setPixelSize(pixel);
	while (pixel > 22
		&& QFontMetricsF(font).horizontalAdvance(text) > width - 40) {
		pixel -= 4;
		font.setPixelSize(pixel);
	}
	p.setFont(font);
	p.translate(width / 2., height / 2. + 8.);
	p.rotate(-4. + unit(random) * 8.);
	p.setPen(QColor(110, 6, 8, 235));
	p.drawText(
		QRectF(-width / 2., -height / 2., width, height),
		Qt::AlignCenter,
		text);
	p.resetTransform();
	p.setPen(Qt::NoPen);
	auto pin = QRadialGradient(QPointF(width / 2. - 4, 20), 12);
	pin.setColorAt(0., QColor(240, 90, 90));
	pin.setColorAt(1., QColor(90, 10, 10));
	p.setBrush(pin);
	p.drawEllipse(QPointF(width / 2., 22), 9, 9);
	p.end();
	return result;
}

struct ForestLook {
	QColor skyTop;
	QColor skyMid;
	QColor fog;
	QColor groundNear;
	QColor groundFar;
	QColor leafDark;
	QColor leafLight;
	QColor trunk;
	QColor disc;
	QColor glow;
	int stars = 260;
	double discElevation = 0.42;
	double discSize = 0.035;
	double fogDensity = 0.05;
};

[[nodiscard]] ForestLook MakeForestLook(int timeOfDay, int fog) {
	auto result = ForestLook();
	switch (timeOfDay) {
	case 1:
		result.skyTop = QColor(20, 14, 44);
		result.skyMid = QColor(156, 66, 72);
		result.fog = QColor(150, 86, 78);
		result.groundNear = QColor(42, 28, 32);
		result.groundFar = QColor(12, 8, 12);
		result.leafDark = QColor(10, 14, 14);
		result.leafLight = QColor(22, 26, 24);
		result.trunk = QColor(20, 12, 10);
		result.disc = QColor(255, 178, 96);
		result.glow = QColor(255, 120, 70, 130);
		result.stars = 50;
		result.discElevation = 0.05;
		result.discSize = 0.06;
		break;
	case 2:
		result.skyTop = QColor(58, 118, 190);
		result.skyMid = QColor(150, 192, 224);
		result.fog = QColor(176, 198, 210);
		result.groundNear = QColor(72, 94, 70);
		result.groundFar = QColor(34, 50, 36);
		result.leafDark = QColor(20, 56, 34);
		result.leafLight = QColor(38, 86, 52);
		result.trunk = QColor(62, 44, 30);
		result.disc = QColor(255, 250, 226);
		result.glow = QColor(255, 255, 220, 120);
		result.stars = 0;
		result.discElevation = 0.8;
		result.discSize = 0.03;
		break;
	default:
		result.skyTop = QColor(3, 5, 9);
		result.skyMid = QColor(16, 24, 30);
		result.fog = QColor(40, 54, 60);
		result.groundNear = QColor(22, 30, 32);
		result.groundFar = QColor(8, 11, 12);
		result.leafDark = QColor(14, 34, 24);
		result.leafLight = QColor(24, 50, 36);
		result.trunk = QColor(28, 20, 15);
		result.disc = QColor(214, 222, 228);
		result.glow = QColor(150, 170, 190, 90);
		break;
	}
	result.fogDensity = 0.05 * Clamp(fog, 0, 100) / 50.;
	return result;
}

void PaintSky(
		QPainter &p,
		const Camera &camera,
		QSize full,
		const ForestLook &look) {
	const auto fog = look.fog;
	const auto horizon = camera.horizonY();
	auto sky = QLinearGradient(0, horizon - camera.focal() * 1.2, 0, horizon);
	sky.setColorAt(0., look.skyTop);
	sky.setColorAt(0.75, look.skyMid);
	sky.setColorAt(1., fog);
	p.fillRect(QRect(QPoint(), full), sky);

	auto random = std::mt19937(4242);
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != look.stars; ++i) {
		const auto azimuth = unit(random) * 2. * kPi;
		const auto elevation = 0.12 + unit(random) * 1.3;
		const auto direction = Vec{
			std::sin(azimuth) * std::cos(elevation) * 900.,
			kEyeHeight + std::sin(elevation) * 900.,
			std::cos(azimuth) * std::cos(elevation) * 900.,
		};
		auto depth = 0.;
		const auto point = camera.project(direction, &depth);
		if (depth <= 1.) {
			continue;
		}
		const auto r = 0.5 + unit(random) * 0.9;
		p.setBrush(QColor(200, 210, 230, int(50 + unit(random) * 120)));
		p.drawEllipse(point, r, r);
	}

	auto depth = 0.;
	const auto moonAzimuth = 0.7;
	const auto moonElevation = look.discElevation;
	const auto moon = camera.project({
		std::sin(moonAzimuth) * std::cos(moonElevation) * 900.,
		kEyeHeight + std::sin(moonElevation) * 900.,
		std::cos(moonAzimuth) * std::cos(moonElevation) * 900.,
	}, &depth);
	if (depth > 1.) {
		const auto r = camera.focal() * look.discSize;
		auto glow = QRadialGradient(moon, r * 9.);
		glow.setColorAt(0., look.glow);
		glow.setColorAt(1., WithAlpha(look.glow, 0.));
		p.setBrush(glow);
		p.drawEllipse(moon, r * 9., r * 9.);
		p.setBrush(look.disc);
		p.drawEllipse(moon, r, r);
	}

	auto ground = QLinearGradient(0, horizon, 0, full.height());
	ground.setColorAt(0., fog);
	ground.setColorAt(0.08, look.groundNear);
	ground.setColorAt(1., look.groundFar);
	p.fillRect(
		QRectF(0, horizon, full.width(), std::max(0., full.height() - horizon)),
		ground);
}

void PaintTree(
		QPainter &p,
		const Camera &camera,
		const Tree &tree,
		double depth,
		const ForestLook &look,
		const std::vector<QString> &notes) {
	const auto fog = look.fog;
	const auto fogAmount = 1. - std::exp(-depth * look.fogDensity);
	const auto k = camera.focal() / std::max(depth, kNearPlane);
	const auto base = camera.project({ tree.x, 0., tree.z });
	const auto trunkTop = camera.project({ tree.x, tree.height * 0.4, tree.z });

	const auto trunk = Mix(look.trunk, fog, fogAmount);
	p.setPen(QPen(trunk, std::max(1., 0.28 * k), Qt::SolidLine, Qt::FlatCap));
	p.drawLine(base, trunkTop);

	const auto leaf = Mix(
		Mix(look.leafDark, look.leafLight, tree.shade),
		fog,
		fogAmount);
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != tree.tiers; ++i) {
		const auto progress = double(i) / tree.tiers;
		const auto from = 0.1 + 0.86 * progress;
		const auto till = std::min(1.03, from + 0.27);
		const auto bottom = camera.project({ tree.x, tree.height * from, tree.z });
		const auto top = camera.project({ tree.x, tree.height * till, tree.z });
		const auto half = (tree.width * std::pow(1. - progress, 0.9) + 0.35)
			* k / 2.;
		const auto droop = (bottom.y() - top.y()) * 0.12;

		auto polygon = QPolygonF();
		polygon << top;
		for (auto s = 1; s <= 3; ++s) {
			const auto t = s / 4.;
			const auto edge = top + (QPointF(bottom.x() + half, bottom.y()) - top) * t;
			polygon << edge + QPointF(half * 0.1, 0.);
			polygon << edge + QPointF(-half * 0.06, half * 0.04);
		}
		polygon << QPointF(bottom.x() + half, bottom.y());
		polygon << QPointF(bottom.x(), bottom.y() - droop);
		polygon << QPointF(bottom.x() - half, bottom.y());
		for (auto s = 3; s >= 1; --s) {
			const auto t = s / 4.;
			const auto edge = top + (QPointF(bottom.x() - half, bottom.y()) - top) * t;
			polygon << edge + QPointF(half * 0.06, half * 0.04);
			polygon << edge + QPointF(-half * 0.1, 0.);
		}
		p.setBrush(Mix(leaf, QColor(0, 0, 0), 0.3 * (1. - progress)));
		p.drawPolygon(polygon);
	}

	if (tree.note < 0 || depth > 16. || notes.empty()) {
		return;
	}
	const auto length = std::sqrt(tree.x * tree.x + tree.z * tree.z);
	const auto toward = Vec{ -tree.x / length, 0., -tree.z / length };
	const auto side = Vec{ -toward.z, 0., toward.x };
	const auto center = Vec{
		tree.x + toward.x * 0.17,
		tree.noteHeight,
		tree.z + toward.z * 0.17,
	};
	const auto angle = tree.noteTilt * kPi / 180.;
	const auto halfW = 0.36;
	const auto halfH = 0.25;
	const auto corner = [&](double sx, double sy) {
		const auto lx = sx * halfW * std::cos(angle) - sy * halfH * std::sin(angle);
		const auto ly = sx * halfW * std::sin(angle) + sy * halfH * std::cos(angle);
		return camera.project({
			center.x + side.x * lx,
			center.y + ly,
			center.z + side.z * lx,
		});
	};
	auto quad = QPolygonF();
	quad << corner(-1, 1) << corner(1, 1) << corner(1, -1) << corner(-1, -1);
	const auto texture = NoteTexture(notes[tree.note % int(notes.size())]);
	auto source = QPolygonF();
	source << QPointF(0, 0)
		<< QPointF(texture.width(), 0)
		<< QPointF(texture.width(), texture.height())
		<< QPointF(0, texture.height());
	auto transform = QTransform();
	if (!QTransform::quadToQuad(source, quad, transform)) {
		return;
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.setTransform(transform, true);
	p.setOpacity(0.92);
	p.drawImage(QPointF(), texture);
	p.restore();
	p.setBrush(WithAlpha(fog, fogAmount * 0.8 + 0.08));
	p.drawPolygon(quad);
}

void RenderForest(
		RoomFrame &frame,
		const RoomParams &params,
		const Camera &camera,
		QSize full) {
	const auto look = MakeForestLook(params.timeOfDay, params.fog);
	const auto notes = ParseNotes(params.notes);
	const auto fog = look.fog;
	frame.fog = fog;
	struct Visible {
		const Tree *tree = nullptr;
		double depth = 0.;
	};
	auto visible = std::vector<Visible>();
	const auto &trees = ForestTrees();
	const auto limit = std::clamp(
		int(std::round(params.trees * Clamp(params.density, 0, 100) / 100.)),
		5,
		int(trees.size()));
	for (auto index = 0; index != limit; ++index) {
		const auto &tree = trees[index];
		const auto camera3 = camera.toCamera({ tree.x, tree.height * 0.5, tree.z });
		if (camera3.z < 0.4) {
			continue;
		}
		const auto point = camera.fromCamera(camera3);
		const auto halfWidth = tree.width * camera.focal() / camera3.z;
		if (point.x() + halfWidth < -frame.margin
			|| point.x() - halfWidth > full.width() + frame.margin) {
			continue;
		}
		visible.push_back({ &tree, camera3.z });
	}
	std::sort(visible.begin(), visible.end(), [](const Visible &a, const Visible &b) {
		return a.depth > b.depth;
	});

	struct Band {
		double from = 0.;
		double till = 0.;
		double depth = 0.;
	};
	const Band bands[] = {
		{ 18., 1000., 30. },
		{ 7., 18., 11. },
		{ 0., 7., 4. },
	};
	const auto makeImage = [&] {
		auto image = QImage(
			QSize(
				int(std::ceil(full.width() * RenderRatio(params))),
				int(std::ceil(full.height() * RenderRatio(params)))),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(RenderRatio(params));
		image.fill(Qt::transparent);
		return image;
	};
	{
		auto image = makeImage();
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		PaintSky(p, camera, full, look);
		p.end();
		frame.layers.push_back({ std::move(image), 0. });
	}
	for (const auto &band : bands) {
		auto image = makeImage();
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		for (const auto &entry : visible) {
			if (entry.depth >= band.from && entry.depth < band.till) {
				PaintTree(p, camera, *entry.tree, entry.depth, look, notes);
			}
		}
		if (band.depth > 20.) {
			const auto horizon = camera.horizonY();
			auto mist = QLinearGradient(0, horizon - camera.focal() * 0.12, 0, horizon + camera.focal() * 0.06);
			mist.setColorAt(0., WithAlpha(fog, 0.));
			mist.setColorAt(0.6, WithAlpha(fog, 0.55));
			mist.setColorAt(1., WithAlpha(fog, 0.));
			p.fillRect(QRect(QPoint(), full), mist);
		}
		p.end();
		frame.layers.push_back({ std::move(image), band.depth });
	}
}

void DrawNeonLine(
		QPainter &p,
		const Camera &camera,
		Vec a,
		Vec b,
		const QColor &color,
		double width,
		double glow = 1.) {
	auto ca = camera.toCamera(a);
	auto cb = camera.toCamera(b);
	if (!camera.clipLine(ca, cb)) {
		return;
	}
	const auto depth = std::max((ca.z + cb.z) / 2., 0.3);
	const auto fade = std::exp(-depth * 0.07);
	const auto pa = camera.fromCamera(ca);
	const auto pb = camera.fromCamera(cb);
	p.setPen(QPen(WithAlpha(color, 0.18 * glow * fade), width * 4., Qt::SolidLine, Qt::RoundCap));
	p.drawLine(pa, pb);
	p.setPen(QPen(WithAlpha(color, 0.9 * fade), width, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(pa, pb);
}

void RenderNeon(
		RoomFrame &frame,
		const RoomParams &params,
		const Camera &camera,
		QSize full) {
	frame.fog = QColor(20, 6, 34);
	auto image = QImage(
		QSize(
			int(std::ceil(full.width() * RenderRatio(params))),
			int(std::ceil(full.height() * RenderRatio(params)))),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(RenderRatio(params));
	auto p = QPainter(&image);
	p.setRenderHint(QPainter::Antialiasing);
	auto background = QLinearGradient(0, 0, 0, full.height());
	background.setColorAt(0., QColor(8, 2, 18));
	background.setColorAt(1., QColor(18, 4, 28));
	p.fillRect(QRect(QPoint(), full), background);

	constexpr auto kHalf = 8.;
	constexpr auto kHeight = 4.6;
	const auto floorColor = params.accent.isValid()
		? params.accent
		: QColor(255, 47, 208);
	const auto wallColor = QColor(34, 224, 255);
	const auto ceilColor = QColor(150, 70, 255);
	const auto line = std::max(1., camera.focal() / 700.);

	auto sunDepth = 0.;
	const auto sun = camera.project({ 0., 2.3, kHalf - 0.01 }, &sunDepth);
	if (sunDepth > 0.3) {
		const auto r = 1.9 * camera.focal() / sunDepth;
		auto glow = QRadialGradient(sun, r * 2.4);
		glow.setColorAt(0., QColor(255, 80, 160, 90));
		glow.setColorAt(1., QColor(255, 80, 160, 0));
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(sun, r * 2.4, r * 2.4);
		auto disc = QLinearGradient(sun.x(), sun.y() - r, sun.x(), sun.y() + r);
		disc.setColorAt(0., QColor(255, 226, 90));
		disc.setColorAt(1., QColor(255, 40, 150));
		p.setBrush(disc);
		p.drawEllipse(sun, r, r);
		auto clip = QPainterPath();
		clip.addEllipse(sun, r + 1., r + 1.);
		p.save();
		p.setClipPath(clip);
		p.setBrush(QColor(8, 2, 18));
		for (auto i = 0; i != 6; ++i) {
			const auto y = sun.y() + r * (0.1 + i * 0.16);
			const auto h = r * (0.03 + i * 0.018);
			p.drawRect(QRectF(sun.x() - r - 2, y, 2 * r + 4, h));
		}
		p.restore();
	}

	const auto cells = 4 + int(std::round(Clamp(params.density, 0, 100) * 0.12));
	const auto step = 2. * kHalf / cells;
	const auto glow = Clamp(params.fog, 0, 100) / 50.;
	for (auto i = 0; i <= cells; ++i) {
		const auto v = -kHalf + i * step;
		DrawNeonLine(p, camera, { v, 0., -kHalf }, { v, 0., kHalf }, floorColor, line, glow);
		DrawNeonLine(p, camera, { -kHalf, 0., v }, { kHalf, 0., v }, floorColor, line, glow);
		DrawNeonLine(p, camera, { v, kHeight, -kHalf }, { v, kHeight, kHalf }, ceilColor, line * 0.7, glow);
		DrawNeonLine(p, camera, { -kHalf, kHeight, v }, { kHalf, kHeight, v }, ceilColor, line * 0.7, glow);
		for (const auto wall : { -kHalf, kHalf }) {
			DrawNeonLine(p, camera, { wall, 0., v }, { wall, kHeight, v }, wallColor, line * 0.8, glow);
			DrawNeonLine(p, camera, { v, 0., wall }, { v, kHeight, wall }, wallColor, line * 0.8, glow);
		}
	}
	const auto rows = std::max(2, int(std::round(cells * 6. / 16.)));
	for (auto j = 1; j != rows; ++j) {
		const auto y = j * kHeight / rows;
		for (const auto wall : { -kHalf, kHalf }) {
			DrawNeonLine(p, camera, { wall, y, -kHalf }, { wall, y, kHalf }, wallColor, line * 0.6, glow);
			DrawNeonLine(p, camera, { -kHalf, y, wall }, { kHalf, y, wall }, wallColor, line * 0.6, glow);
		}
	}
	p.end();
	frame.layers.push_back({ std::move(image), 6. });
}

[[nodiscard]] QImage MakeLayerImage(const RoomParams &params, QSize full) {
	auto image = QImage(
		QSize(
			int(std::ceil(full.width() * RenderRatio(params))),
			int(std::ceil(full.height() * RenderRatio(params)))),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(RenderRatio(params));
	image.fill(Qt::transparent);
	return image;
}

[[nodiscard]] Vec SkyDirection(double azimuth, double elevation, double distance) {
	return {
		std::sin(azimuth) * std::cos(elevation) * distance,
		kEyeHeight + std::sin(elevation) * distance,
		std::cos(azimuth) * std::cos(elevation) * distance,
	};
}

void PaintPlanet(
		QPainter &p,
		const Camera &camera,
		Vec position,
		double radius,
		QColor color,
		bool ring) {
	auto depth = 0.;
	const auto center = camera.project(position, &depth);
	if (depth <= 0.5) {
		return;
	}
	const auto r = radius * camera.focal() / depth;
	auto rotate = QTransform();
	rotate.translate(center.x(), center.y());
	rotate.rotate(-16.);
	const auto rx = r * 2.15;
	const auto ry = r * 0.42;
	auto ringPath = QPainterPath();
	ringPath.addEllipse(QPointF(), rx, ry);
	auto innerPath = QPainterPath();
	innerPath.addEllipse(QPointF(), rx * 0.72, ry * 0.72);
	ringPath = rotate.map(ringPath.subtracted(innerPath));
	auto backHalf = QPainterPath();
	backHalf.addRect(QRectF(-rx - 2, -ry - 2, 2 * rx + 4, ry + 2));
	auto planetPath = QPainterPath();
	planetPath.addEllipse(center, r, r);
	const auto hidden = planetPath.intersected(rotate.map(backHalf));
	const auto ringColor = Mix(color, QColor(255, 236, 200), 0.45);
	p.setPen(Qt::NoPen);
	auto halo = QRadialGradient(center, r * 1.45);
	halo.setColorAt(0.65, WithAlpha(color, 0.35));
	halo.setColorAt(1., WithAlpha(color, 0.));
	p.setBrush(halo);
	p.drawEllipse(center, r * 1.45, r * 1.45);
	auto body = QRadialGradient(center - QPointF(r * 0.45, r * 0.45), r * 1.6);
	body.setColorAt(0., Mix(color, QColor(255, 255, 255), 0.35));
	body.setColorAt(0.45, color);
	body.setColorAt(1., Mix(color, QColor(0, 0, 0), 0.88));
	p.setBrush(body);
	p.drawEllipse(center, r, r);
	p.save();
	p.setClipPath(planetPath);
	for (auto i = 0; i != 4; ++i) {
		const auto y = center.y() - r * 0.55 + i * r * 0.33;
		p.setBrush(WithAlpha(Mix(color, QColor(0, 0, 0), 0.4), 0.22));
		p.drawRect(QRectF(center.x() - r, y, 2 * r, r * 0.09));
	}
	p.restore();
	if (ring) {
		auto ringFill = QLinearGradient(
			center - QPointF(rx, 0.),
			center + QPointF(rx, 0.));
		ringFill.setColorAt(0., WithAlpha(ringColor, 0.45));
		ringFill.setColorAt(0.5, WithAlpha(ringColor, 0.8));
		ringFill.setColorAt(1., WithAlpha(ringColor, 0.45));
		p.setBrush(ringFill);
		p.drawPath(ringPath.subtracted(hidden));
	}
}

void RenderSpace(
		RoomFrame &frame,
		const RoomParams &params,
		const Camera &camera,
		QSize full) {
	frame.fog = QColor(16, 14, 40);
	const auto accent = params.accent.isValid()
		? params.accent
		: QColor(255, 47, 208);
	auto second = accent.toHsv();
	second.setHsv(
		(std::max(second.hue(), 0) + 150) % 360,
		std::max(second.saturation(), 120),
		std::max(second.value(), 180));
	const auto glow = Clamp(params.fog, 0, 100) / 50.;
	const auto density = Clamp(params.density, 0, 100) / 100.;
	auto random = std::mt19937(2024);
	auto unit = std::uniform_real_distribution<double>(0., 1.);

	{
		auto image = MakeLayerImage(params, full);
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		auto background = QLinearGradient(0, 0, 0, full.height());
		background.setColorAt(0., QColor(2, 3, 12));
		background.setColorAt(1., QColor(10, 6, 24));
		p.fillRect(QRect(QPoint(), full), background);
		p.setPen(Qt::NoPen);
		for (auto i = 0; i != 7; ++i) {
			const auto azimuth = (i / 7.) * 2. * kPi + unit(random) * 0.6;
			const auto elevation = (unit(random) - 0.35) * 0.9;
			auto depth = 0.;
			const auto center = camera.project(
				SkyDirection(azimuth, elevation, 900.),
				&depth);
			if (depth <= 1.) {
				continue;
			}
			const auto r = camera.focal() * (0.35 + unit(random) * 0.45);
			const auto color = (i % 2) ? QColor(second) : accent;
			auto cloud = QRadialGradient(center, r);
			cloud.setColorAt(0., WithAlpha(color, 0.3 * glow));
			cloud.setColorAt(0.5, WithAlpha(color, 0.12 * glow));
			cloud.setColorAt(1., WithAlpha(color, 0.));
			p.setBrush(cloud);
			p.drawEllipse(center, r, r);
		}
		const auto stars = 160 + int(density * 900);
		for (auto i = 0; i != stars; ++i) {
			auto depth = 0.;
			const auto point = camera.project(SkyDirection(
				unit(random) * 2. * kPi,
				(unit(random) * 2. - 1.) * 1.45,
				900.), &depth);
			if (depth <= 1.) {
				continue;
			}
			const auto r = 0.4 + unit(random) * 0.8;
			const auto tint = Mix(QColor(210, 220, 255), accent, unit(random) * 0.25);
			p.setBrush(WithAlpha(tint, 0.25 + unit(random) * 0.6));
			p.drawEllipse(point, r, r);
		}
		p.end();
		frame.layers.push_back({ std::move(image), 0. });
	}
	{
		auto image = MakeLayerImage(params, full);
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		const auto bright = 12 + int(density * 40);
		for (auto i = 0; i != bright; ++i) {
			auto depth = 0.;
			const auto point = camera.project(SkyDirection(
				unit(random) * 2. * kPi,
				(unit(random) * 2. - 1.) * 1.3,
				60.), &depth);
			if (depth <= 1.) {
				continue;
			}
			const auto r = 1.2 + unit(random) * 1.6;
			const auto color = Mix(QColor(255, 255, 255), (i % 3) ? accent : QColor(second), 0.3);
			auto halo = QRadialGradient(point, r * 5.);
			halo.setColorAt(0., WithAlpha(color, 0.45));
			halo.setColorAt(1., WithAlpha(color, 0.));
			p.setPen(Qt::NoPen);
			p.setBrush(halo);
			p.drawEllipse(point, r * 5., r * 5.);
			p.setPen(QPen(WithAlpha(color, 0.5), 1.));
			p.drawLine(point - QPointF(r * 4., 0.), point + QPointF(r * 4., 0.));
			p.drawLine(point - QPointF(0., r * 4.), point + QPointF(0., r * 4.));
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 230));
			p.drawEllipse(point, r * 0.6, r * 0.6);
		}
		p.end();
		frame.layers.push_back({ std::move(image), 30. });
	}
	{
		auto image = MakeLayerImage(params, full);
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		PaintPlanet(p, camera, { 7., 5.5, 16. }, 3.2, accent, true);
		PaintPlanet(p, camera, { -6.5, 3.2, 11. }, 0.9, QColor(second), false);
		PaintPlanet(p, camera, { 12., 1.2, -14. }, 2.4, Mix(accent, QColor(second), 0.5), true);
		PaintPlanet(p, camera, { -14., 6., -8. }, 1.6, QColor(second), false);
		p.end();
		frame.layers.push_back({ std::move(image), 9. });
	}
}

[[nodiscard]] QImage LoadPhoto(const QString &path) {
	static auto cachedPath = QString();
	static auto cached = QImage();
	if (path == cachedPath) {
		return cached;
	}
	auto reader = QImageReader(path);
	reader.setAutoTransform(true);
	const auto size = reader.size();
	constexpr auto kMaxSide = 3840;
	if (size.isValid()
		&& (size.width() > kMaxSide || size.height() > kMaxSide)) {
		reader.setScaledSize(size.scaled(
			kMaxSide,
			kMaxSide,
			Qt::KeepAspectRatio));
	}
	cachedPath = path;
	cached = reader.read();
	return cached;
}

void RenderPhoto(
		RoomFrame &frame,
		const RoomParams &params,
		QSize full) {
	frame.fog = QColor(0, 0, 0);
	const auto ratio = std::max(params.ratio, 0.5);
	const auto target = QSize(
		int(std::ceil(full.width() * ratio)),
		int(std::ceil(full.height() * ratio)));
	auto image = QImage(target, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(QColor(12, 10, 18));
	const auto source = LoadPhoto(params.photo);
	auto p = QPainter(&image);
	if (!source.isNull()) {
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto scaled = source.size().scaled(
			full,
			Qt::KeepAspectRatioByExpanding);
		const auto rect = QRectF(
			(full.width() - scaled.width()) / 2.,
			(full.height() - scaled.height()) / 2.,
			scaled.width(),
			scaled.height());
		p.drawImage(rect, source);
	} else {
		p.setPen(QColor(255, 255, 255, 120));
		p.drawText(
			QRect(QPoint(), full),
			Qt::AlignCenter,
			QString::fromUtf8("Фото не выбрано"));
	}
	const auto dim = Clamp(params.dim, 0, 90) / 100.;
	if (dim > 0.) {
		p.fillRect(QRect(QPoint(), full), QColor(0, 0, 0, int(dim * 255)));
	}
	p.end();
	frame.layers.push_back({ std::move(image), 9. });
}

} // namespace

QString RoomName(RoomType type) {
	switch (type) {
	case RoomType::None: return QString::fromUtf8("Выкл");
	case RoomType::Forest: return QString::fromUtf8("Еловый лес");
	case RoomType::Neon: return QString::fromUtf8("Неоновая комната");
	case RoomType::Custom: return QString::fromUtf8("Своя сцена");
	case RoomType::Photo: return QString::fromUtf8("Фото");
	case RoomType::Video: return QString::fromUtf8("Видео");
	}
	return QString();
}

bool PhotoAvailable(const QString &path) {
	return !path.isEmpty() && QFileInfo(path).isFile();
}

QImage RenderBranch(QSize size, double ratio) {
	auto result = QImage(
		QSize(int(std::ceil(size.width() * ratio)), int(std::ceil(size.height() * ratio))),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	if (size.isEmpty()) {
		return result;
	}
	auto p = QPainter(&result);
	p.setRenderHint(QPainter::Antialiasing);
	auto random = std::mt19937(99);
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	const auto w = double(size.width());
	const auto h = double(size.height());
	const auto needle = std::max(7., w * 0.05);
	const auto drawNeedles = [&](QPointF from, QPointF till, double scale) {
		const auto delta = till - from;
		const auto length = std::hypot(delta.x(), delta.y());
		if (length < 1.) {
			return;
		}
		const auto dir = delta / length;
		const auto normal = QPointF(-dir.y(), dir.x());
		const auto steps = int(length / 1.7);
		for (auto i = 0; i <= steps; ++i) {
			const auto t = double(i) / std::max(steps, 1);
			const auto at = from + delta * t;
			const auto taper = (0.35 + 0.65 * (1. - t)) * scale;
			for (const auto side : { -1., 1. }) {
				const auto spread = 0.55 + 0.25 * unit(random);
				const auto vector = (dir * 0.55 + normal * side * spread)
					* needle * taper * (0.7 + 0.5 * unit(random));
				const auto lit = (side < 0.) ? 0.35 : 0.;
				p.setPen(QPen(
					Mix(QColor(4, 9, 6, 245), QColor(38, 58, 52, 245), lit * t),
					std::max(1.2, needle * 0.11),
					Qt::SolidLine,
					Qt::RoundCap));
				p.drawLine(at, at + vector);
			}
		}
	};
	const auto start = QPointF(-w * 0.02, -h * 0.02);
	const auto end = QPointF(w * 0.98, h * 0.62);
	const auto control = QPointF(w * 0.45, h * 0.05);
	const auto bough = [&](double t) {
		const auto a = start * (1. - t) * (1. - t);
		const auto b = control * 2. * t * (1. - t);
		const auto c = end * t * t;
		return a + b + c;
	};
	auto previous = bough(0.);
	for (auto i = 1; i <= 40; ++i) {
		const auto t = i / 40.;
		const auto point = bough(t);
		p.setPen(QPen(QColor(10, 8, 6, 250), std::max(1.5, w * 0.012 * (1.1 - t)), Qt::SolidLine, Qt::RoundCap));
		p.drawLine(previous, point);
		drawNeedles(previous, point, 1.2);
		previous = point;
	}
	for (auto i = 0; i != 16; ++i) {
		const auto t = 0.06 + i * 0.058;
		const auto from = bough(t);
		const auto ahead = bough(std::min(1., t + 0.05));
		const auto dir = ahead - from;
		const auto len = std::hypot(dir.x(), dir.y());
		const auto unitDir = dir / std::max(len, 1.);
		const auto side = (i % 2) ? 1. : -1.;
		const auto angle = side * (0.6 + 0.3 * unit(random));
		const auto rotated = QPointF(
			unitDir.x() * std::cos(angle) - unitDir.y() * std::sin(angle),
			unitDir.x() * std::sin(angle) + unitDir.y() * std::cos(angle));
		const auto length = w * (0.3 - 0.18 * t) * (0.8 + 0.4 * unit(random));
		const auto till = from + rotated * length + QPointF(0., length * 0.25);
		p.setPen(QPen(QColor(10, 8, 6, 240), std::max(1., w * 0.005), Qt::SolidLine, Qt::RoundCap));
		p.drawLine(from, till);
		drawNeedles(from, till, 0.95);
		const auto twig = from + (till - from) * 0.5;
		const auto twigEnd = twig + rotated * length * 0.35 + QPointF(side * length * 0.15, length * 0.2);
		drawNeedles(twig, twigEnd, 0.7);
	}
	p.end();
	return result;
}

RoomFrame RenderRoom(const RoomParams &params, QSize size) {
	auto result = RoomFrame();
	if (params.type == RoomType::None || size.isEmpty()) {
		return result;
	}
	result.size = size;
	result.margin = std::max(24, int(std::max(size.width(), size.height()) * 0.08));
	const auto full = size + QSize(result.margin * 2, result.margin * 2);
	const auto camera = Camera(params, size, result.margin);
	result.focal = camera.focal();
	switch (params.type) {
	case RoomType::Forest:
		RenderForest(result, params, camera, full);
		break;
	case RoomType::Neon:
		RenderNeon(result, params, camera, full);
		break;
	case RoomType::Custom:
		switch (params.base) {
		case SceneBase::Neon:
			RenderNeon(result, params, camera, full);
			break;
		case SceneBase::Space:
			RenderSpace(result, params, camera, full);
			break;
		default:
			RenderForest(result, params, camera, full);
			break;
		}
		break;
	case RoomType::Photo:
		RenderPhoto(result, params, full);
		break;
	case RoomType::Video:
		result.fog = QColor(0, 0, 0);
		result.video = true;
		break;
	case RoomType::None:
		break;
	}
	const auto target = QSize(
		int(std::ceil(full.width() * params.ratio)),
		int(std::ceil(full.height() * params.ratio)));
	for (auto &layer : result.layers) {
		if (layer.image.size() != target) {
			layer.image = layer.image.scaled(
				target,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
			layer.image.setDevicePixelRatio(params.ratio);
		}
	}
	result.vignette = QImage(
		QSize(
			int(std::ceil(size.width() * params.ratio)),
			int(std::ceil(size.height() * params.ratio))),
		QImage::Format_ARGB32_Premultiplied);
	result.vignette.setDevicePixelRatio(params.ratio);
	result.vignette.fill(Qt::transparent);
	{
		auto p = QPainter(&result.vignette);
		const auto center = QPointF(size.width() / 2., size.height() / 2.);
		const auto radius = std::hypot(size.width(), size.height()) * 0.62;
		auto vignette = QRadialGradient(center, radius);
		vignette.setColorAt(0.55, QColor(0, 0, 0, 0));
		vignette.setColorAt(1., QColor(0, 0, 0, 150));
		p.fillRect(QRectF(QPointF(), QSizeF(size)), vignette);
	}
	return result;
}

void PaintRoomFrame(
		QPainter &p,
		const RoomFrame &frame,
		QSize fill,
		QPointF cameraShift,
		double time,
		bool motion) {
	if (frame.layers.empty() && !frame.video) {
		return;
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const auto sx = frame.size.isEmpty()
		? 1.
		: fill.width() / double(frame.size.width());
	const auto sy = frame.size.isEmpty()
		? 1.
		: fill.height() / double(frame.size.height());
	p.scale(sx, sy);
	const auto margin = double(frame.margin);
	for (const auto &layer : frame.layers) {
		auto offset = QPointF();
		if (layer.depth > 0.) {
			offset = QPointF(
				std::round(Clamp(-cameraShift.x() * frame.focal / layer.depth, -margin, margin)),
				std::round(Clamp(cameraShift.y() * frame.focal / layer.depth, -margin, margin)));
		}
		const auto target = QRectF(
			offset.x() - margin,
			offset.y() - margin,
			frame.size.width() + margin * 2,
			frame.size.height() + margin * 2);
		p.drawImage(target, layer.image);
	}
	if (motion) {
		p.setPen(Qt::NoPen);
		const auto w = double(frame.size.width());
		const auto h = double(frame.size.height());
		for (auto i = 0; i != 4; ++i) {
			const auto phase = i * 1.7;
			const auto cx = std::fmod(time * (8. + i * 3.) + i * w * 0.37, w * 1.6) - w * 0.3;
			const auto cy = h * (0.45 + 0.12 * i) + std::sin(time * 0.2 + phase) * h * 0.03;
			const auto rx = w * (0.35 + 0.08 * i);
			const auto ry = h * 0.09;
			auto puff = QRadialGradient(QPointF(cx, cy), rx);
			puff.setColorAt(0., WithAlpha(frame.fog, 0.22));
			puff.setColorAt(1., WithAlpha(frame.fog, 0.));
			p.save();
			p.translate(cx, cy);
			p.scale(1., ry / rx);
			p.translate(-cx, -cy);
			p.setBrush(puff);
			p.drawEllipse(QPointF(cx, cy), rx, rx);
			p.restore();
		}
	}
	if (!frame.vignette.isNull()) {
		p.drawImage(QRectF(QPointF(), QSizeF(frame.size)), frame.vignette);
	}
	p.restore();
}

} // namespace Mwtk
