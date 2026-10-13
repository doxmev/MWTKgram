/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mwtk/mwtk_scene.h"
#include "mwtk/mwtk_seasons.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>

namespace Ui {
class RpWidgetWrap;
} // namespace Ui

namespace Mwtk {

class VisualsWidget final : public Ui::RpWidget {
public:
	VisualsWidget(QWidget *parent, bool followOptions);
	~VisualsWidget();

	void setConfig(const SceneConfig &config);
	void setRunning(bool running);
	void burst(QPointF at);
	// A glowing userpic flies over the background to the chat list.
	void flyby(QImage userpic);

	// Draws everything; used both by paintEvent() and the GPU surface.
	void paintContent(QPainter &p, const QRegion &clip);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Ripple {
		QPointF center;
		double progress = 0.;
	};
	struct ButtonState {
		QPointer<QWidget> target;
		QRectF rect;
		double alpha = 0.;
		double press = 0.;
		QPointF light;
		bool pressed = false;
		std::vector<Ripple> ripples;
		QRect lastDirty;
	};

	void tick();
	void wake();
	void refreshFromOptions();
	[[nodiscard]] bool canAnimate() const;
	[[nodiscard]] bool sceneAllowed() const;
	[[nodiscard]] bool buttonsSettled() const;
	[[nodiscard]] QRegion tickButtons(double dt);
	[[nodiscard]] QRegion tickFrame(double dt);
	[[nodiscard]] QWidget *findHoveredButton() const;
	void paintButtons(QPainter &p) const;
	void paintFrame(QPainter &p) const;
	void paintTrail(QPainter &p) const;
	void paintBurst(QPainter &p) const;
	[[nodiscard]] QRegion tickBurst(double dt);
	void pushTrailPoint();
	[[nodiscard]] QRegion tickTrail();
	void refreshFrameCache();
	void refreshGpu();
	void requestRepaint();
	void requestRepaint(const QRegion &region);

	Scene _scene;
	Seasons _seasons;
	bool _seasonsEnabled = false;
	float64 _seasonsDt = 0.;
	base::Timer _timer;
	crl::time _lastTick = 0;
	bool _running = false;
	bool _followOptions = false;
	bool _sceneEnabled = false;
	bool _buttons3d = false;
	bool _buttonsAnimated = false;
	bool _frameEnabled = false;
	bool _frameAnimated = false;
	crl::time _frameDelay = 33;
	ButtonState _button;
	QPoint _lastCursor;
	QPointer<QWidget> _lastHovered;

	struct TrailPoint {
		QPointF position;
		crl::time at = 0;
	};
	bool _trailEnabled = false;
	std::vector<TrailPoint> _trail;
	QRect _trailDirty;

	struct Spark {
		QPointF position;
		QPointF velocity;
		double life = 1.;
		double size = 2.;
		int color = 0;
	};
	struct Shock {
		QPointF center;
		double progress = 0.;
	};
	bool _burstEnabled = false;
	std::vector<Spark> _sparks;
	std::vector<Shock> _shocks;
	QRect _burstDirty;

	struct Flyby {
		QImage userpic;
		QPointF from;
		QPointF to;
		double progress = 0.;
		double arc = 0.;
	};
	std::vector<Flyby> _flybys;
	QRect _flybyDirty;
	[[nodiscard]] QRegion tickFlybys(double dt);
	void paintFlybys(QPainter &p) const;

	QImage _branch;
	QSize _branchFor;
	QPointF _frameShift;
	double _frameTime = 0.;
	double _levelAverage = 0.;
	double _pulse = 0.;
	std::vector<QRect> _frameRects;

	std::unique_ptr<Ui::RpWidgetWrap> _gpu;

};

void SetupOverlay(not_null<Ui::RpWidget*> body);

} // namespace Mwtk
