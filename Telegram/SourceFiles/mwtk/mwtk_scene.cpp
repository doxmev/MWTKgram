/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_scene.h"

#include <QtGui/QFont>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QRadialGradient>
#include <QtGui/QTransform>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Mwtk {
namespace {

constexpr auto kPi = 3.14159265358979323846;
constexpr auto kRainbowIndex = 6;

struct PaletteData {
	const char *name;
	QRgb first;
	QRgb second;
	QRgb third;
	QRgb glow;
};

const PaletteData kPalettes[] = {
	{ "Кровавокрасный", 0xff7a0000, 0xffff1f1f, 0xffff6a4d, 0xffff0000 },
	{ "Розовый", 0xffff2fa6, 0xffff8ad8, 0xffc13bff, 0xffff4fc1 },
	{ "Чёрный", 0xff050505, 0xff3a3a3a, 0xff8a8a8a, 0xffffffff },
	{ "Белый", 0xffffffff, 0xffdbe7ff, 0xfff4f4f4, 0xffffffff },
	{ "Синий", 0xff0047ff, 0xff2fd2ff, 0xff6a5cff, 0xff1f8bff },
	{ "Жёлтый", 0xffffc400, 0xfffff27a, 0xffff9100, 0xffffd400 },
	{ "Радуга", 0, 0, 0, 0 },
};

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

[[nodiscard]] QImage BlurImage(const QImage &image, int factor) {
	const auto reduced = image.scaled(
		std::max(1, image.width() / factor),
		std::max(1, image.height() / factor),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	const auto smaller = reduced.scaled(
		std::max(1, reduced.width() / 2),
		std::max(1, reduced.height() / 2),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	return smaller.scaled(
		image.size(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

void Rotate(std::array<double, 3> &v, const double angles[3]) {
	const auto cx = std::cos(angles[0]), sx = std::sin(angles[0]);
	const auto cy = std::cos(angles[1]), sy = std::sin(angles[1]);
	const auto cz = std::cos(angles[2]), sz = std::sin(angles[2]);
	auto y = v[1] * cx - v[2] * sx;
	auto z = v[1] * sx + v[2] * cx;
	v[1] = y;
	v[2] = z;
	auto x = v[0] * cy + v[2] * sy;
	z = -v[0] * sy + v[2] * cy;
	v[0] = x;
	v[2] = z;
	x = v[0] * cz - v[1] * sz;
	y = v[0] * sz + v[1] * cz;
	v[0] = x;
	v[1] = y;
}

} // namespace

int PalettesCount() {
	return int(std::size(kPalettes));
}

Palette PaletteAt(int index, double time) {
	if (index < 0 || index >= PalettesCount()) {
		index = 0;
	}
	if (index == kRainbowIndex) {
		const auto hue = std::fmod(time * 45., 360.);
		const auto at = [&](double shift) {
			return QColor::fromHsvF(
				float(std::fmod(hue + shift, 360.) / 360.),
				0.85f,
				1.f);
		};
		return { at(0.), at(70.), at(140.), at(35.) };
	}
	const auto &data = kPalettes[index];
	return {
		QColor::fromRgba(data.first),
		QColor::fromRgba(data.second),
		QColor::fromRgba(data.third),
		QColor::fromRgba(data.glow),
	};
}

QString PaletteName(int index) {
	if (index < 0 || index >= PalettesCount()) {
		return QString();
	}
	return QString::fromUtf8(kPalettes[index].name);
}

Scene::Scene() : _random(20261011) {
	const auto phi = (1. + std::sqrt(5.)) / 2.;

	auto cube = Shape();
	for (auto i = 0; i != 8; ++i) {
		cube.vertices.push_back({
			(i & 1) ? 1. : -1.,
			(i & 2) ? 1. : -1.,
			(i & 4) ? 1. : -1.,
		});
	}
	for (auto i = 0; i != 8; ++i) {
		for (auto j = i + 1; j != 8; ++j) {
			const auto diff = i ^ j;
			if (diff == 1 || diff == 2 || diff == 4) {
				cube.edges.emplace_back(i, j);
			}
		}
	}
	for (auto &v : cube.vertices) {
		for (auto &c : v) {
			c *= 0.6;
		}
	}

	auto ico = Shape();
	for (const auto a : { -1., 1. }) {
		for (const auto b : { -phi, phi }) {
			ico.vertices.push_back({ 0., a, b });
			ico.vertices.push_back({ a, b, 0. });
			ico.vertices.push_back({ b, 0., a });
		}
	}
	const auto icoLength = std::sqrt(1. + phi * phi);
	for (auto &v : ico.vertices) {
		for (auto &c : v) {
			c /= icoLength;
		}
	}
	for (auto i = 0; i != int(ico.vertices.size()); ++i) {
		for (auto j = i + 1; j != int(ico.vertices.size()); ++j) {
			const auto &a = ico.vertices[i];
			const auto &b = ico.vertices[j];
			const auto d = std::sqrt(
				(a[0] - b[0]) * (a[0] - b[0])
				+ (a[1] - b[1]) * (a[1] - b[1])
				+ (a[2] - b[2]) * (a[2] - b[2]));
			if (d < 1.1) {
				ico.edges.emplace_back(i, j);
			}
		}
	}

	auto octa = Shape();
	octa.vertices = {
		{ 1., 0., 0. }, { -1., 0., 0. },
		{ 0., 1., 0. }, { 0., -1., 0. },
		{ 0., 0., 1. }, { 0., 0., -1. },
	};
	for (auto i = 0; i != 6; ++i) {
		for (auto j = i + 1; j != 6; ++j) {
			if ((i / 2) != (j / 2)) {
				octa.edges.emplace_back(i, j);
			}
		}
	}

	auto torus = Shape();
	constexpr auto kMajor = 16;
	constexpr auto kMinor = 7;
	for (auto i = 0; i != kMajor; ++i) {
		const auto u = 2. * kPi * i / kMajor;
		for (auto j = 0; j != kMinor; ++j) {
			const auto v = 2. * kPi * j / kMinor;
			const auto r = 0.72 + 0.28 * std::cos(v);
			torus.vertices.push_back({
				r * std::cos(u),
				r * std::sin(u),
				0.28 * std::sin(v),
			});
			const auto index = i * kMinor + j;
			torus.edges.emplace_back(
				index,
				((i + 1) % kMajor) * kMinor + j);
			torus.edges.emplace_back(
				index,
				i * kMinor + (j + 1) % kMinor);
		}
	}

	_shapes.push_back(std::move(cube));
	_shapes.push_back(std::move(ico));
	_shapes.push_back(std::move(octa));
	_shapes.push_back(std::move(torus));
}

void Scene::setConfig(const SceneConfig &config) {
	const auto textChanged = (_config.label != config.label)
		|| (_config.scale != config.scale)
		|| (_config.palette != config.palette);
	const auto scaleChanged = (_config.scale != config.scale)
		|| (_config.density != config.density);
	_config = config;
	if (textChanged) {
		rebuildText();
	}
	if (scaleChanged) {
		rebuildBodies();
	}
}

void Scene::resize(QSize size) {
	if (_size == size) {
		return;
	}
	_size = size;
	rebuildText();
	rebuildBodies();
}

QSize Scene::size() const {
	return _size;
}

bool Scene::empty() const {
	return _size.isEmpty()
		|| (!_config.text && !_config.objects && !_config.particles);
}

bool Scene::enabled(Kind kind) const {
	switch (kind) {
	case Kind::Label: return _config.text;
	case Kind::Object: return _config.objects;
	case Kind::Particle: return _config.particles;
	}
	return false;
}

double Scene::focal() const {
	return std::max(_size.width(), _size.height()) * 0.5;
}

void Scene::rebuildText() {
	_textPath = QPainterPath();
	_textGlow = QImage();
	if (_size.isEmpty() || _config.label.isEmpty()) {
		return;
	}
	const auto pixel = int(std::round(24. * _config.scale));
	auto font = QFont(QStringLiteral("Segoe UI"));
	font.setPixelSize(pixel);
	font.setWeight(QFont::Black);
	font.setItalic(true);
	font.setLetterSpacing(QFont::PercentageSpacing, 104);
	auto path = QPainterPath();
	path.addText(0., 0., font, _config.label);
	const auto bounds = path.boundingRect();
	_textPath = path.translated(-bounds.center());
	_textBounds = _textPath.boundingRect();

	const auto palette = PaletteAt(_config.palette, 0.);
	const auto margin = int(std::ceil(pixel * 0.75));
	const auto glowRect = _textBounds.toAlignedRect().marginsAdded(
		{ margin, margin, margin, margin });
	auto glow = QImage(glowRect.size(), QImage::Format_ARGB32_Premultiplied);
	glow.fill(Qt::transparent);
	{
		auto p = QPainter(&glow);
		p.setRenderHint(QPainter::Antialiasing);
		p.translate(-glowRect.topLeft());
		const auto color = (_config.palette == kRainbowIndex)
			? QColor(255, 255, 255)
			: palette.glow;
		p.setPen(QPen(WithAlpha(color, 0.9), pixel * 0.18));
		p.setBrush(color);
		p.drawPath(_textPath);
	}
	_textGlow = BlurImage(glow, std::max(2, pixel / 5));
	_textGlowOffset = glowRect.topLeft();
}

double Scene::farPlane(Kind kind) {
	switch (kind) {
	case Kind::Label: return 2.4;
	case Kind::Object: return 3.2;
	case Kind::Particle: return 4.2;
	}
	return 4.2;
}

void Scene::spawn(Body &body, bool initial) {
	constexpr auto kNear = 0.55;
	const auto kFar = farPlane(body.kind);
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	body.z = initial ? (kNear + 0.4 + unit(_random) * (kFar - kNear - 0.4)) : kFar;
	const auto w = std::max(1, _size.width());
	const auto h = std::max(1, _size.height());
	const auto f = std::max(1., focal());
	const auto spreadX = (w / 2.) * body.z / f * 1.1;
	const auto spreadY = (h / 2.) * body.z / f * 1.1;
	body.x = (unit(_random) * 2. - 1.) * spreadX;
	body.y = (unit(_random) * 2. - 1.) * spreadY;
	body.phase = unit(_random) * 2. * kPi;
	body.color = int(unit(_random) * 3.) % 3;
	switch (body.kind) {
	case Kind::Label:
		body.vz = -(0.035 + 0.025 * unit(_random));
		body.size = 0.9 + 0.3 * unit(_random);
		break;
	case Kind::Object:
		body.vz = -(0.045 + 0.04 * unit(_random));
		body.size = 0.8 + 0.6 * unit(_random);
		for (auto k = 0; k != 3; ++k) {
			body.angles[k] = unit(_random) * 2. * kPi;
			body.speeds[k] = (0.06 + 0.22 * unit(_random))
				* ((unit(_random) < 0.5) ? -1. : 1.);
		}
		break;
	case Kind::Particle:
		body.vz = -(0.06 + 0.08 * unit(_random));
		body.size = 0.6 + 0.9 * unit(_random);
		break;
	}
	body.vx = (unit(_random) * 2. - 1.) * 0.012;
	body.vy = (unit(_random) * 2. - 1.) * 0.012;
}

void Scene::rebuildBodies() {
	_bodies.clear();
	if (_size.isEmpty()) {
		return;
	}
	const auto area = double(_size.width()) * _size.height();
	const auto density = Clamp(_config.density, 0.2, 1.);
	const auto labels = int(Clamp(area / 120000., 5., 10.) * density + 0.5);
	const auto objects = int(Clamp(area / 75000., 7., 16.) * density + 0.5);
	const auto particles = int(Clamp(area / 12000., 30., 80.) * density + 0.5);
	const int shapes[] = { 0, 1, 0, 2, 0, 3, 0, 1 };
	auto add = [&](Kind kind, int count) {
		for (auto i = 0; i != count; ++i) {
			auto body = Body();
			body.kind = kind;
			body.shape = shapes[i % int(std::size(shapes))];
			spawn(body, true);
			_bodies.push_back(body);
		}
	};
	add(Kind::Particle, particles);
	add(Kind::Object, objects);
	add(Kind::Label, labels);
}

Scene::Projected Scene::project(const Body &body) const {
	const auto f = focal();
	const auto camX = 0.06 * std::sin(_time * 0.21);
	const auto camY = 0.045 * std::sin(_time * 0.17 + 1.1);
	const auto z = std::max(body.z, 0.05);
	auto result = Projected();
	result.center = QPointF(
		_size.width() / 2. + (body.x - camX) * f / z,
		_size.height() / 2. + (body.y - camY) * f / z);
	result.scale = 1. / z;
	const auto smooth = [](double edge0, double edge1, double x) {
		const auto t = Clamp((x - edge0) / (edge1 - edge0), 0., 1.);
		return t * t * (3. - 2. * t);
	};
	const auto farZ = farPlane(body.kind);
	const auto fadeIn = 1. - smooth(farZ - 0.8, farZ, body.z);
	const auto fadeOut = smooth(0.55, 1.05, body.z);
	const auto depth = 0.55 + 0.45 * (1. - smooth(1., 4.2, body.z));
	result.alpha = fadeIn * fadeOut * depth;
	return result;
}

QRect Scene::bodyRect(const Body &body) const {
	const auto projected = project(body);
	if (projected.alpha <= 0.) {
		return QRect();
	}
	const auto c = projected.center;
	switch (body.kind) {
	case Kind::Label: {
		if (_textGlow.isNull()) {
			return QRect();
		}
		const auto k = projected.scale * body.size;
		const auto r = std::max(_textGlow.width(), _textGlow.height())
			* k * 0.56 + 3.;
		return QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r).toAlignedRect();
	}
	case Kind::Object: {
		const auto r = 30. * _config.scale * body.size * projected.scale
			* 1.5 + 4.;
		return QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r).toAlignedRect();
	}
	case Kind::Particle: {
		const auto r = 2.2 * _config.scale * body.size * projected.scale
			* 2.4 + 2.;
		return QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r).toAlignedRect();
	}
	}
	return QRect();
}

void Scene::advance(double seconds) {
	const auto dt = Clamp(seconds, 0., 0.1);
	_time += dt;
	const auto f = std::max(1., focal());
	for (auto &body : _bodies) {
		body.x += body.vx * dt;
		body.y += body.vy * dt;
		body.z += body.vz * dt;
		for (auto k = 0; k != 3; ++k) {
			body.angles[k] += body.speeds[k] * dt;
		}
		const auto limitX = (_size.width() / 2.) * body.z / f * 1.35 + 0.2;
		const auto limitY = (_size.height() / 2.) * body.z / f * 1.35 + 0.2;
		if (body.z < 0.55
			|| std::abs(body.x) > limitX
			|| std::abs(body.y) > limitY) {
			spawn(body, false);
		}
	}
}

void Scene::collectRects(std::vector<QRect> &rects) const {
	for (const auto &body : _bodies) {
		if (!enabled(body.kind)) {
			continue;
		}
		const auto rect = bodyRect(body);
		if (!rect.isEmpty()) {
			rects.push_back(rect);
		}
	}
}

QRegion Scene::takeDirtyRegion() {
	auto rects = std::vector<QRect>();
	collectRects(rects);
	constexpr auto kTile = 32;
	const auto snap = [&](const QRect &rect) {
		const auto left = (rect.left() / kTile) * kTile;
		const auto top = (rect.top() / kTile) * kTile;
		const auto right = ((rect.right() + kTile) / kTile) * kTile;
		const auto bottom = ((rect.bottom() + kTile) / kTile) * kTile;
		return QRect(left, top, right - left, bottom - top);
	};
	auto result = QRegion();
	for (const auto &rect : _lastRects) {
		result += snap(rect);
	}
	for (const auto &rect : rects) {
		result += snap(rect);
	}
	_lastRects = std::move(rects);
	return result;
}

void Scene::paint(QPainter &p) const {
	if (empty()) {
		return;
	}
	const auto palette = PaletteAt(_config.palette, _time);
	auto order = std::vector<const Body*>();
	order.reserve(_bodies.size());
	for (const auto &body : _bodies) {
		if (enabled(body.kind)) {
			order.push_back(&body);
		}
	}
	std::sort(order.begin(), order.end(), [](const Body *a, const Body *b) {
		return a->z > b->z;
	});
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	for (const auto body : order) {
		const auto projected = project(*body);
		if (projected.alpha <= 0.003) {
			continue;
		}
		p.setOpacity(_config.opacity * projected.alpha);
		switch (body->kind) {
		case Kind::Label: paintLabel(p, *body, projected, palette); break;
		case Kind::Object: paintObject(p, *body, projected, palette); break;
		case Kind::Particle:
			paintParticle(p, *body, projected, palette);
			break;
		}
	}
	p.restore();
}

void Scene::paintLabel(
		QPainter &p,
		const Body &body,
		const Projected &projected,
		const Palette &palette) const {
	if (_textPath.isEmpty()) {
		return;
	}
	const auto opacity = p.opacity();
	p.save();
	p.translate(projected.center);
	p.rotate(5. * std::sin(_time * 0.35 + body.phase));
	const auto k = projected.scale * body.size;
	p.scale(k, k);

	const auto pulse = 0.5 + 0.5 * std::sin(_time * 1.3 + body.phase);
	p.setOpacity(opacity * (0.5 + 0.35 * pulse));
	p.drawImage(_textGlowOffset, _textGlow);
	p.setOpacity(opacity);

	const auto width = std::max(1., _textBounds.width());
	const auto shift = std::fmod(
		(_time * 0.6 + body.phase) * width * 0.45,
		width * 2.);
	auto gradient = QLinearGradient(
		_textBounds.left() - shift,
		0.,
		_textBounds.left() - shift + width * 2.,
		0.);
	gradient.setSpread(QGradient::RepeatSpread);
	gradient.setColorAt(0., palette.first);
	gradient.setColorAt(0.33, palette.second);
	gradient.setColorAt(0.66, palette.third);
	gradient.setColorAt(1., palette.first);
	p.fillPath(_textPath, gradient);

	const auto period = 4.5;
	const auto phase = std::fmod(_time + body.phase * 2., period) / period;
	const auto sweepX = _textBounds.left()
		+ (phase * 1.8 - 0.4) * _textBounds.width();
	auto shine = QLinearGradient(
		sweepX - width * 0.2,
		_textBounds.top(),
		sweepX + width * 0.2,
		_textBounds.bottom());
	shine.setColorAt(0., QColor(255, 255, 255, 0));
	shine.setColorAt(0.5, QColor(255, 255, 255, 150));
	shine.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillPath(_textPath, shine);
	p.restore();
}

void Scene::paintObject(
		QPainter &p,
		const Body &body,
		const Projected &projected,
		const Palette &palette) const {
	const auto &shape = _shapes[body.shape];
	const auto radius = 30. * _config.scale * body.size * projected.scale;
	struct Point {
		QPointF point;
		double depth = 0.;
	};
	auto points = std::vector<Point>();
	points.reserve(shape.vertices.size());
	const auto distance = 3.4;
	for (auto vertex : shape.vertices) {
		Rotate(vertex, body.angles);
		const auto k = distance / (distance - vertex[2]);
		points.push_back({
			projected.center + QPointF(vertex[0], vertex[1]) * radius * k,
			(vertex[2] + 1.) / 2.,
		});
	}
	struct Edge {
		int from = 0;
		int till = 0;
		double depth = 0.;
	};
	auto edges = std::vector<Edge>();
	edges.reserve(shape.edges.size());
	for (const auto &[from, till] : shape.edges) {
		edges.push_back({
			from,
			till,
			(points[from].depth + points[till].depth) / 2.,
		});
	}
	std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b) {
		return a.depth < b.depth;
	});
	const auto line = std::max(0.6, 1.3 * _config.scale * projected.scale);
	const auto tint = 0.5 + 0.5 * std::sin(_time * 0.4 + body.phase);
	for (const auto pass : { 0, 1 }) {
		for (const auto &edge : edges) {
			const auto depth = Clamp(edge.depth, 0., 1.);
			const auto base = Mix(palette.third, palette.first, depth);
			const auto color = Mix(base, palette.second, tint * 0.5);
			const auto width = line * (0.6 + 0.6 * depth);
			p.setPen(pass
				? QPen(
					WithAlpha(color, 0.3 + 0.7 * depth),
					width,
					Qt::SolidLine,
					Qt::RoundCap)
				: QPen(
					WithAlpha(palette.glow, 0.08 + 0.14 * depth),
					width * 3.,
					Qt::SolidLine,
					Qt::RoundCap));
			p.drawLine(points[edge.from].point, points[edge.till].point);
		}
	}
}

void Scene::paintParticle(
		QPainter &p,
		const Body &body,
		const Projected &projected,
		const Palette &palette) const {
	const QColor colors[] = { palette.first, palette.second, palette.third };
	const auto twinkle = 0.45
		+ 0.55 * (0.5 + 0.5 * std::sin(_time * 1.4 + body.phase));
	const auto r = 2.2 * _config.scale * body.size * projected.scale * 2.4;
	auto gradient = QRadialGradient(projected.center, r);
	const auto &color = colors[body.color];
	gradient.setColorAt(0., WithAlpha(QColor(255, 255, 255), twinkle));
	gradient.setColorAt(0.25, WithAlpha(color, twinkle * 0.85));
	gradient.setColorAt(1., WithAlpha(color, 0.));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawEllipse(projected.center, r, r);
}

} // namespace Mwtk
