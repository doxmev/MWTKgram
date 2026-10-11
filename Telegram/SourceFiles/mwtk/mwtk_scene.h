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
	QString label = QStringLiteral("doxmev");
};

class Scene final {
public:
	Scene();

	void setConfig(const SceneConfig &config);
	void resize(QSize size);
	void advance(double seconds);
	void paint(QPainter &p) const;

	[[nodiscard]] QRegion takeDirtyRegion();
	[[nodiscard]] bool empty() const;
	[[nodiscard]] QSize size() const;

private:
	struct Shape {
		std::vector<std::array<double, 3>> vertices;
		std::vector<std::pair<int, int>> edges;
	};
	struct Object {
		int shape = 0;
		QPointF position;
		QPointF velocity;
		double radius = 0.;
		double angles[3] = { 0., 0., 0. };
		double speeds[3] = { 0., 0., 0. };
	};
	struct Particle {
		QPointF position;
		double speed = 0.;
		double phase = 0.;
		double size = 0.;
		int color = 0;
	};

	void rebuildText();
	void rebuildObjects();
	void rebuildParticles();
	void collectRects(std::vector<QRect> &rects) const;
	[[nodiscard]] QRect textRect() const;
	[[nodiscard]] QPointF textPosition() const;
	void paintText(QPainter &p, const Palette &palette) const;
	void paintObject(
		QPainter &p,
		const Object &object,
		const Palette &palette) const;
	void paintParticles(QPainter &p, const Palette &palette) const;

	SceneConfig _config;
	QSize _size;
	double _time = 0.;
	std::mt19937 _random;

	QPainterPath _textPath;
	QRectF _textBounds;
	QImage _textGlow;
	QPointF _textGlowOffset;
	int _textGlowPalette = -1;

	std::vector<Shape> _shapes;
	std::vector<Object> _objects;
	std::vector<Particle> _particles;

	std::vector<QRect> _lastRects;

};

} // namespace Mwtk
