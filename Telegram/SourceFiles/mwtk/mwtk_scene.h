/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QPointF>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainterPath>
#include <QtGui/QRegion>

#include <array>
#include <random>
#include <vector>

class QPainter;

namespace Mwtk {

struct Palette {
	QColor first;
	QColor second;
	QColor third;
	QColor glow;
};

[[nodiscard]] int PalettesCount();
[[nodiscard]] Palette PaletteAt(int index, double time);
[[nodiscard]] QString PaletteName(int index);

struct SceneConfig {
	bool text = true;
	bool objects = true;
	bool particles = true;
	int palette = 0;
	double opacity = 0.6;
	double scale = 1.;
	double density = 1.;
	bool lite = false;
	QString label = QStringLiteral("doxmev");
};

class Scene final {
public:
	Scene();

	void setConfig(const SceneConfig &config);
	void resize(QSize size);
	void advance(double seconds);
	void paint(QPainter &p) const;
	void paint(QPainter &p, const QRegion &clip) const;

	[[nodiscard]] QRegion takeDirtyRegion();
	[[nodiscard]] bool empty() const;
	[[nodiscard]] QSize size() const;

private:
	enum class Kind {
		Label,
		Object,
		Particle,
	};
	struct Shape {
		std::vector<std::array<double, 3>> vertices;
		std::vector<std::pair<int, int>> edges;
	};
	struct Body {
		Kind kind = Kind::Particle;
		double x = 0.;
		double y = 0.;
		double z = 1.;
		double vx = 0.;
		double vy = 0.;
		double vz = 0.;
		double size = 1.;
		double phase = 0.;
		int shape = 0;
		int color = 0;
		double angles[3] = { 0., 0., 0. };
		double speeds[3] = { 0., 0., 0. };
	};
	struct Projected {
		QPointF center;
		double scale = 0.;
		double alpha = 0.;
	};

	void rebuildText();
	void rebuildBodies();
	void spawn(Body &body, bool initial);
	[[nodiscard]] static double farPlane(Kind kind);
	[[nodiscard]] bool enabled(Kind kind) const;
	[[nodiscard]] double focal() const;
	[[nodiscard]] Projected project(const Body &body) const;
	[[nodiscard]] QRect bodyRect(const Body &body) const;
	void collectRects(std::vector<QRect> &rects) const;

	void paintLabel(
		QPainter &p,
		const Body &body,
		const Projected &projected,
		const Palette &palette) const;
	void paintObject(
		QPainter &p,
		const Body &body,
		const Projected &projected,
		const Palette &palette) const;
	void paintParticle(
		QPainter &p,
		const Body &body,
		const Projected &projected,
		const Palette &palette) const;

	SceneConfig _config;
	QSize _size;
	double _time = 0.;
	std::mt19937 _random;

	QPainterPath _textPath;
	QRectF _textBounds;
	QImage _textGlow;
	QPointF _textGlowOffset;
	mutable QImage _textImage;
	mutable int _textImagePalette = -1;

	std::vector<Shape> _shapes;
	std::vector<Body> _bodies;
	std::vector<QRect> _lastRects;

};

} // namespace Mwtk
