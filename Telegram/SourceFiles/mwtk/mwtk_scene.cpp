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
	const auto small = image.scaled(
		std::max(1, image.width() / factor),
		std::max(1, image.height() / factor),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	const auto smaller = small.scaled(
		std::max(1, small.width() / 2),
		std::max(1, small.height() / 2),
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
			c *= 0.62;
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

	_shapes.push_back(std::move(ico));
	_shapes.push_back(std::move(torus));
	_shapes.push_back(std::move(cube));
	_shapes.push_back(std::move(octa));
}

void Scene::setConfig(const SceneConfig &config) {
	const auto textChanged = (_config.label != config.label)
		|| (_config.scale != config.scale)
		|| (_config.palette != config.palette);
	const auto scaleChanged = (_config.scale != config.scale);
	_config = config;
	if (textChanged) {
		rebuildText();
	}
	if (scaleChanged) {
		rebuildObjects();
		rebuildParticles();
	}
}

void Scene::resize(QSize size) {
	if (_size == size) {
		return;
	}
	_size = size;
	rebuildText();
	rebuildObjects();
	rebuildParticles();
}

QSize Scene::size() const {
	return _size;
}

bool Scene::empty() const {
	return _size.isEmpty()
		|| (!_config.text && !_config.objects && !_config.particles);
}

void Scene::rebuildText() {
	_textPath = QPainterPath();
	_textGlow = QImage();
	if (_size.isEmpty() || _config.label.isEmpty()) {
		return;
	}
	const auto shortest = std::min(_size.width(), _size.height());
	const auto pixel = int(Clamp(
		shortest * 0.13,
		26. * _config.scale,
		84. * _config.scale));
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
	const auto margin = int(std::ceil(pixel * 0.55));
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
		p.setPen(QPen(WithAlpha(color, 0.9), pixel * 0.16));
		p.setBrush(color);
		p.drawPath(_textPath);
	}
	_textGlow = BlurImage(glow, std::max(2, pixel / 9));
	_textGlowOffset = glowRect.topLeft();
	_textGlowPalette = _config.palette;
}

void Scene::rebuildObjects() {
	_objects.clear();
	if (_size.isEmpty()) {
		return;
	}
	const auto area = double(_size.width()) * _size.height();
	const auto count = int(Clamp(area / 170000., 3., 6.));
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	for (auto i = 0; i != count; ++i) {
		auto object = Object();
		object.shape = i % int(_shapes.size());
		object.radius = (46. + 38. * unit(_random)) * _config.scale;
		object.position = QPointF(
			object.radius + unit(_random) * std::max(1., _size.width() - 2 * object.radius),
			object.radius + unit(_random) * std::max(1., _size.height() - 2 * object.radius));
		const auto angle = unit(_random) * 2. * kPi;
		const auto speed = (16. + 18. * unit(_random)) * _config.scale;
		object.velocity = QPointF(
			std::cos(angle) * speed,
			std::sin(angle) * speed);
		for (auto k = 0; k != 3; ++k) {
			object.angles[k] = unit(_random) * 2. * kPi;
			object.speeds[k] = (0.15 + 0.5 * unit(_random))
				* ((unit(_random) < 0.5) ? -1. : 1.);
		}
		_objects.push_back(object);
	}
}

void Scene::rebuildParticles() {
	_particles.clear();
	if (_size.isEmpty()) {
		return;
	}
	const auto area = double(_size.width()) * _size.height();
	const auto count = int(Clamp(area / 16000., 18., 70.));
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	for (auto i = 0; i != count; ++i) {
		auto particle = Particle();
		particle.position = QPointF(
			unit(_random) * _size.width(),
			unit(_random) * _size.height());
		particle.speed = (10. + 28. * unit(_random)) * _config.scale;
		particle.phase = unit(_random) * 2. * kPi;
		particle.size = (1.6 + 3.2 * unit(_random)) * _config.scale;
		particle.color = i % 3;
		_particles.push_back(particle);
	}
}

void Scene::advance(double seconds) {
	const auto dt = Clamp(seconds, 0., 0.1);
	_time += dt;
	const auto width = double(_size.width());
	const auto height = double(_size.height());
	for (auto &object : _objects) {
		object.position += object.velocity * dt;
		const auto r = object.radius * 1.1;
		if (object.position.x() < r) {
			object.position.setX(r);
			object.velocity.setX(std::abs(object.velocity.x()));
		} else if (object.position.x() > width - r) {
			object.position.setX(std::max(r, width - r));
			object.velocity.setX(-std::abs(object.velocity.x()));
		}
		if (object.position.y() < r) {
			object.position.setY(r);
			object.velocity.setY(std::abs(object.velocity.y()));
		} else if (object.position.y() > height - r) {
			object.position.setY(std::max(r, height - r));
			object.velocity.setY(-std::abs(object.velocity.y()));
		}
		for (auto k = 0; k != 3; ++k) {
			object.angles[k] += object.speeds[k] * dt;
		}
	}
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	for (auto &particle : _particles) {
		particle.position.ry() -= particle.speed * dt;
		particle.position.rx() += std::sin(_time * 0.9 + particle.phase)
			* 9. * _config.scale * dt;
		if (particle.position.y() < -10.) {
			particle.position = QPointF(
				unit(_random) * width,
				height + 10.);
		}
	}
	if (_config.palette == kRainbowIndex && _textGlowPalette != kRainbowIndex) {
		rebuildText();
	}
}

QPointF Scene::textPosition() const {
	const auto w = double(_size.width());
	const auto h = double(_size.height());
	const auto ax = std::max(0., (w - _textBounds.width()) / 2. - 30.) * 0.85;
	const auto ay = std::max(0., (h - _textBounds.height()) / 2. - 30.) * 0.7;
	return QPointF(
		w / 2. + ax * std::sin(_time * 0.21),
		h / 2. + ay * std::sin(_time * 0.29 + 1.3));
}

QRect Scene::textRect() const {
	if (_textGlow.isNull()) {
		return QRect();
	}
	auto transform = QTransform();
	const auto position = textPosition();
	transform.translate(position.x(), position.y());
	transform.rotate(4. * std::sin(_time * 0.7));
	const auto scale = 1. + 0.04 * std::sin(_time * 1.6);
	transform.scale(scale, scale);
	return transform.mapRect(
		QRectF(_textGlowOffset, QSizeF(_textGlow.size()))
	).toAlignedRect().marginsAdded({ 2, 2, 2, 2 });
}

void Scene::collectRects(std::vector<QRect> &rects) const {
	if (_config.text) {
		const auto rect = textRect();
		if (!rect.isEmpty()) {
			rects.push_back(rect);
		}
	}
	if (_config.objects) {
		for (const auto &object : _objects) {
			const auto r = object.radius * 1.45 + 4.;
			rects.push_back(QRectF(
				object.position.x() - r,
				object.position.y() - r,
				2 * r,
				2 * r).toAlignedRect());
		}
	}
	if (_config.particles) {
		for (const auto &particle : _particles) {
			const auto r = particle.size * 3. + 2.;
			rects.push_back(QRectF(
				particle.position.x() - r,
				particle.position.y() - r,
				2 * r,
				2 * r).toAlignedRect());
		}
	}
}

QRegion Scene::takeDirtyRegion() {
	auto rects = std::vector<QRect>();
	collectRects(rects);
	auto result = QRegion();
	for (const auto &rect : _lastRects) {
		result += rect;
	}
	for (const auto &rect : rects) {
		result += rect;
	}
	_lastRects = std::move(rects);
	return result;
}

void Scene::paint(QPainter &p) const {
	if (empty()) {
		return;
	}
	const auto palette = PaletteAt(_config.palette, _time);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	if (_config.particles) {
		p.setOpacity(_config.opacity);
		paintParticles(p, palette);
	}
	if (_config.objects) {
		p.setOpacity(_config.opacity);
		for (const auto &object : _objects) {
			paintObject(p, object, palette);
		}
	}
	if (_config.text) {
		p.setOpacity(_config.opacity);
		paintText(p, palette);
	}
	p.restore();
}

void Scene::paintText(QPainter &p, const Palette &palette) const {
	if (_textPath.isEmpty()) {
		return;
	}
	const auto position = textPosition();
	const auto opacity = p.opacity();
	p.save();
	p.translate(position);
	p.rotate(4. * std::sin(_time * 0.7));
	const auto scale = 1. + 0.04 * std::sin(_time * 1.6);
	p.scale(scale, scale);

	const auto pulse = 0.55 + 0.45 * std::sin(_time * 2.1);
	p.setOpacity(opacity * (0.45 + 0.4 * pulse));
	p.drawImage(_textGlowOffset, _textGlow);
	p.setOpacity(opacity);

	const auto width = std::max(1., _textBounds.width());
	const auto shift = std::fmod(_time * width * 0.45, width * 2.);
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

	const auto period = 3.2;
	const auto phase = std::fmod(_time, period) / period;
	const auto sweepX = _textBounds.left()
		+ (phase * 1.8 - 0.4) * _textBounds.width();
	auto shine = QLinearGradient(
		sweepX - width * 0.18,
		_textBounds.top(),
		sweepX + width * 0.18,
		_textBounds.bottom());
	shine.setColorAt(0., QColor(255, 255, 255, 0));
	shine.setColorAt(0.5, QColor(255, 255, 255, 170));
	shine.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillPath(_textPath, shine);

	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(WithAlpha(palette.glow, 0.55), 1.2 * _config.scale));
	p.drawPath(_textPath);
	p.restore();
}

void Scene::paintObject(
		QPainter &p,
		const Object &object,
		const Palette &palette) const {
	const auto &shape = _shapes[object.shape];
	struct Projected {
		QPointF point;
		double depth = 0.;
	};
	auto projected = std::vector<Projected>();
	projected.reserve(shape.vertices.size());
	const auto distance = 3.4;
	for (auto vertex : shape.vertices) {
		Rotate(vertex, object.angles);
		const auto k = distance / (distance - vertex[2]);
		projected.push_back({
			object.position + QPointF(vertex[0], vertex[1]) * object.radius * k,
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
			(projected[from].depth + projected[till].depth) / 2.,
		});
	}
	std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b) {
		return a.depth < b.depth;
	});
	const auto hueShift = std::sin(_time * 0.8 + object.radius) * 0.5 + 0.5;
	for (const auto pass : { 0, 1 }) {
		for (const auto &edge : edges) {
			const auto depth = Clamp(edge.depth, 0., 1.);
			const auto base = Mix(palette.third, palette.first, depth);
			const auto color = Mix(base, palette.second, hueShift * 0.5);
			const auto width = (1.1 + 1.5 * depth) * _config.scale;
			if (pass == 0) {
				p.setPen(QPen(
					WithAlpha(palette.glow, 0.10 + 0.18 * depth),
					width * 3.2,
					Qt::SolidLine,
					Qt::RoundCap));
			} else {
				p.setPen(QPen(
					WithAlpha(color, 0.35 + 0.65 * depth),
					width,
					Qt::SolidLine,
					Qt::RoundCap));
			}
			p.drawLine(
				projected[edge.from].point,
				projected[edge.till].point);
		}
	}
	p.setPen(Qt::NoPen);
	for (const auto &vertex : projected) {
		if (vertex.depth < 0.45) {
			continue;
		}
		const auto r = (1.2 + 2.2 * vertex.depth) * _config.scale;
		p.setBrush(WithAlpha(palette.second, vertex.depth));
		p.drawEllipse(vertex.point, r, r);
	}
}

void Scene::paintParticles(QPainter &p, const Palette &palette) const {
	p.setPen(Qt::NoPen);
	const QColor colors[] = { palette.first, palette.second, palette.third };
	for (const auto &particle : _particles) {
		const auto twinkle = 0.35
			+ 0.65 * (0.5 + 0.5 * std::sin(_time * 2.3 + particle.phase));
		const auto r = particle.size * 3.;
		auto gradient = QRadialGradient(particle.position, r);
		const auto &color = colors[particle.color];
		gradient.setColorAt(0., WithAlpha(QColor(255, 255, 255), twinkle));
		gradient.setColorAt(0.25, WithAlpha(color, twinkle * 0.9));
		gradient.setColorAt(1., WithAlpha(color, 0.));
		p.setBrush(gradient);
		p.drawEllipse(particle.position, r, r);
	}
}

} // namespace Mwtk
