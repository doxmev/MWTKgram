/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mwtk/mwtk_scene.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>

namespace Mwtk {

class VisualsWidget final : public Ui::RpWidget {
public:
	VisualsWidget(QWidget *parent, bool followOptions);

	void setConfig(const SceneConfig &config);
	void setRunning(bool running);

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
	void pushTrailPoint();
	[[nodiscard]] QRegion tickTrail();
	void refreshFrameCache();

	Scene _scene;
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

	struct TrailPoint {
		QPointF position;
		crl::time at = 0;
	};
	bool _trailEnabled = false;
	std::vector<TrailPoint> _trail;
	QRect _trailDirty;

	QImage _branch;
	QSize _branchFor;
	QPointF _frameShift;
	double _frameTime = 0.;
	std::vector<QRect> _frameRects;

};

void SetupOverlay(not_null<Ui::RpWidget*> body);

} // namespace Mwtk
