/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Mwtk {

// Seasonal layer over the background: snow in winter, leaves in autumn,
// petals in spring, a garland in late December and fireworks at midnight
// on New Year. Uses the local date.
class Seasons final {
public:
	enum class Kind {
		None,
		Snow,
		Leaves,
		Petals,
	};

	void resize(QSize size, double scale);
	void advance(double dt);
	void paint(QPainter &p) const;
	[[nodiscard]] QRegion takeDirtyRegion();
	[[nodiscard]] bool active() const;

	[[nodiscard]] static Kind CurrentKind();
	[[nodiscard]] static bool GarlandTime();
	[[nodiscard]] static bool FireworksTime();

private:
	struct Flake {
		QPointF position;
		QPointF velocity;
		double size = 3.;
		double angle = 0.;
		double spin = 0.;
		double sway = 0.;
		int color = 0;
	};
	struct Spark {
		QPointF position;
		QPointF velocity;
		double life = 1.;
		QColor color;
		bool rocket = false;
	};

	void refreshKind();
	void spawn(Flake &flake, bool initial);
	void launchRocket();
	void explode(QPointF at);
	void paintGarland(QPainter &p) const;
	[[nodiscard]] QRect flakeRect(const Flake &flake) const;

	QSize _size;
	double _scale = 1.;
	double _time = 0.;
	double _checkIn = 0.;
	Kind _kind = Kind::None;
	bool _garland = false;
	bool _fireworks = false;
	double _rocketIn = 0.;
	std::vector<Flake> _flakes;
	std::vector<Spark> _sparks;
	QRegion _lastDirty;

};

} // namespace Mwtk
