/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_seasons.h"

#include <QtCore/QDateTime>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>

#include <array>
#include <cmath>
#include <random>

namespace Mwtk {
namespace {

constexpr auto kPi = 3.14159265358979323846;
constexpr auto kFlakes = 56;

[[nodiscard]] std::mt19937 &Random() {
	static auto result = std::mt19937(2024);
	return result;
}

[[nodiscard]] double Unit() {
	static auto distribution = std::uniform_real_distribution<double>(0., 1.);
	return distribution(Random());
}

[[nodiscard]] QColor LeafColor(int index) {
	switch (index % 4) {
	case 0: return QColor(214, 92, 28);
	case 1: return QColor(232, 160, 40);
	case 2: return QColor(178, 44, 30);
	}
	return QColor(150, 98, 40);
}

[[nodiscard]] QColor PetalColor(int index) {
	return (index % 2) ? QColor(255, 196, 214) : QColor(255, 226, 236);
}

} // namespace

Seasons::Kind Seasons::CurrentKind() {
	const auto month = QDate::currentDate().month();
	if (month == 12 || month <= 2) {
		return Kind::Snow;
	} else if (month >= 9) {
		return Kind::Leaves;
	} else if (month >= 3 && month <= 5) {
		return Kind::Petals;
	}
	return Kind::None;
}

bool Seasons::GarlandTime() {
	const auto date = QDate::currentDate();
	return (date.month() == 12 && date.day() >= 20)
		|| (date.month() == 1 && date.day() <= 10);
}

bool Seasons::FireworksTime() {
	const auto now = QDateTime::currentDateTime();
	const auto date = now.date();
	const auto time = now.time();
	return (date.month() == 12 && date.day() == 31 && time.hour() == 23
			&& time.minute() >= 55)
		|| (date.month() == 1 && date.day() == 1 && time.hour() == 0
			&& time.minute() < 30);
}

void Seasons::resize(QSize size, double scale) {
	_size = size;
	_scale = scale;
	refreshKind();
}

void Seasons::refreshKind() {
	const auto kind = CurrentKind();
	_garland = GarlandTime();
	_fireworks = FireworksTime();
	if (kind != _kind || int(_flakes.size()) != (kind == Kind::None ? 0 : kFlakes)) {
		_kind = kind;
		_flakes.clear();
		if (_kind != Kind::None) {
			_flakes.resize(kFlakes);
			for (auto &flake : _flakes) {
				spawn(flake, true);
			}
		}
	}
}

bool Seasons::active() const {
	return !_size.isEmpty()
		&& (_kind != Kind::None || _garland || _fireworks || !_sparks.empty());
}

void Seasons::spawn(Flake &flake, bool initial) {
	const auto w = std::max(_size.width(), 1);
	const auto h = std::max(_size.height(), 1);
	flake.position = QPointF(
		Unit() * w,
		initial ? Unit() * h : -12. * _scale);
	switch (_kind) {
	case Kind::Snow:
		flake.size = (1.4 + Unit() * 2.4) * _scale;
		flake.velocity = QPointF((Unit() - 0.5) * 12., 18. + Unit() * 26.) * _scale;
		break;
	case Kind::Leaves:
		flake.size = (5. + Unit() * 4.) * _scale;
		flake.velocity = QPointF((Unit() - 0.3) * 18., 22. + Unit() * 22.) * _scale;
		break;
	case Kind::Petals:
		flake.size = (3. + Unit() * 2.5) * _scale;
		flake.velocity = QPointF((Unit() - 0.2) * 20., 16. + Unit() * 18.) * _scale;
		break;
	case Kind::None:
		break;
	}
	flake.angle = Unit() * 2. * kPi;
	flake.spin = (Unit() - 0.5) * 2.;
	flake.sway = Unit() * 2. * kPi;
	flake.color = int(Unit() * 4.);
}

void Seasons::launchRocket() {
	const auto w = std::max(_size.width(), 1);
	const auto h = std::max(_size.height(), 1);
	_sparks.push_back({
		.position = QPointF(w * (0.15 + Unit() * 0.7), h + 4.),
		.velocity = QPointF((Unit() - 0.5) * 40., -(h * 0.75 + Unit() * h * 0.3)) ,
		.life = 0.9 + Unit() * 0.4,
		.color = QColor(255, 230, 200),
		.rocket = true,
	});
}

void Seasons::explode(QPointF at) {
	static const auto colors = std::array<QColor, 5>{
		QColor(255, 60, 80),
		QColor(255, 210, 60),
		QColor(80, 220, 255),
		QColor(160, 90, 255),
		QColor(90, 255, 140),
	};
	const auto color = colors[int(Unit() * colors.size()) % colors.size()];
	for (auto i = 0; i != 46; ++i) {
		const auto angle = Unit() * 2. * kPi;
		const auto speed = (60. + Unit() * 140.) * _scale;
		_sparks.push_back({
			.position = at,
			.velocity = QPointF(std::cos(angle), std::sin(angle)) * speed,
			.life = 0.8 + Unit() * 0.6,
			.color = color,
		});
	}
}

void Seasons::advance(double dt) {
	_time += dt;
	_checkIn -= dt;
	if (_checkIn <= 0.) {
		_checkIn = 30.;
		refreshKind();
	}
	const auto h = double(_size.height());
	for (auto &flake : _flakes) {
		const auto sway = std::sin(_time * 1.3 + flake.sway)
			* ((_kind == Kind::Snow) ? 8. : 22.) * _scale;
		flake.position += (flake.velocity + QPointF(sway, 0.)) * dt;
		flake.angle += flake.spin * dt;
		if (flake.position.y() > h + 12. * _scale
			|| flake.position.x() < -30.
			|| flake.position.x() > _size.width() + 30.) {
			spawn(flake, false);
		}
	}
	if (_fireworks) {
		_rocketIn -= dt;
		if (_rocketIn <= 0.) {
			_rocketIn = 0.6 + Unit() * 1.2;
			launchRocket();
		}
	}
	auto exploded = std::vector<QPointF>();
	for (auto &spark : _sparks) {
		spark.life -= dt / (spark.rocket ? 1.2 : 1.4);
		if (spark.rocket) {
			spark.velocity.ry() += 160. * _scale * dt;
			if (spark.velocity.y() > -40. || spark.life <= 0.) {
				exploded.push_back(spark.position);
				spark.life = 0.;
			}
		} else {
			spark.velocity *= std::exp(-dt * 1.4);
			spark.velocity.ry() += 70. * _scale * dt;
		}
		spark.position += spark.velocity * dt;
	}
	_sparks.erase(
		std::remove_if(_sparks.begin(), _sparks.end(), [](const Spark &s) {
			return s.life <= 0.;
		}),
		_sparks.end());
	for (const auto &at : exploded) {
		explode(at);
	}
}

QRect Seasons::flakeRect(const Flake &flake) const {
	const auto r = int(std::ceil(flake.size * 2. + 3.));
	return QRect(
		int(flake.position.x()) - r,
		int(flake.position.y()) - r,
		r * 2,
		r * 2);
}

QRegion Seasons::takeDirtyRegion() {
	auto now = QRegion();
	for (const auto &flake : _flakes) {
		now += flakeRect(flake);
	}
	for (const auto &spark : _sparks) {
		const auto r = int(6. * _scale) + 2;
		now += QRect(
			int(spark.position.x()) - r,
			int(spark.position.y()) - r,
			r * 2,
			r * 2);
	}
	if (_garland) {
		now += QRect(0, 0, _size.width(), int(34. * _scale));
	}
	auto result = _lastDirty + now;
	_lastDirty = now;
	return result;
}

void Seasons::paintGarland(QPainter &p) const {
	static const auto colors = std::array<QColor, 4>{
		QColor(255, 60, 70),
		QColor(255, 200, 60),
		QColor(70, 220, 120),
		QColor(80, 170, 255),
	};
	const auto w = double(_size.width());
	const auto step = 34. * _scale;
	const auto sag = 12. * _scale;
	auto wire = QPainterPath();
	wire.moveTo(0., 4. * _scale);
	for (auto x = 0.; x < w; x += step * 2.) {
		wire.quadTo(x + step, 4. * _scale + sag * 2., x + step * 2., 4. * _scale);
	}
	p.setPen(QPen(QColor(30, 40, 30, 200), 1.2 * _scale));
	p.setBrush(Qt::NoBrush);
	p.drawPath(wire);
	auto index = 0;
	for (auto x = step / 2.; x < w; x += step / 2., ++index) {
		const auto period = std::fmod(x, step * 2.) / (step * 2.);
		const auto y = 4. * _scale + sag * 4. * period * (1. - period) * 2. * 0.5;
		const auto twinkle = 0.55 + 0.45 * std::sin(_time * 3. + index * 1.7);
		auto color = colors[index % colors.size()];
		auto glow = QRadialGradient(QPointF(x, y + 4. * _scale), 9. * _scale);
		auto c0 = color;
		c0.setAlphaF(float(0.55 * twinkle));
		auto c1 = color;
		c1.setAlphaF(0.f);
		glow.setColorAt(0., c0);
		glow.setColorAt(1., c1);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(QPointF(x, y + 4. * _scale), 9. * _scale, 9. * _scale);
		color.setAlphaF(float(0.6 + 0.4 * twinkle));
		p.setBrush(color);
		p.drawEllipse(QPointF(x, y + 4. * _scale), 2.6 * _scale, 3.4 * _scale);
	}
}

void Seasons::paint(QPainter &p) const {
	if (!active()) {
		return;
	}
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	for (const auto &flake : _flakes) {
		switch (_kind) {
		case Kind::Snow:
			p.setBrush(QColor(255, 255, 255, 200));
			p.drawEllipse(flake.position, flake.size, flake.size);
			break;
		case Kind::Leaves:
		case Kind::Petals: {
			p.save();
			p.translate(flake.position);
			p.rotate(flake.angle * 180. / kPi);
			p.setBrush((_kind == Kind::Leaves)
				? LeafColor(flake.color)
				: PetalColor(flake.color));
			auto leaf = QPainterPath();
			const auto s = flake.size;
			leaf.moveTo(-s, 0.);
			leaf.quadTo(0., -s * 0.7, s, 0.);
			leaf.quadTo(0., s * 0.7, -s, 0.);
			p.drawPath(leaf);
			p.restore();
		} break;
		case Kind::None:
			break;
		}
	}
	for (const auto &spark : _sparks) {
		auto color = spark.color;
		color.setAlphaF(float(std::clamp(spark.life, 0., 1.)));
		p.setBrush(color);
		const auto r = (spark.rocket ? 2.4 : 1.8) * _scale;
		p.drawEllipse(spark.position, r, r);
	}
	if (_garland) {
		paintGarland(p);
	}
	p.restore();
}

} // namespace Mwtk
