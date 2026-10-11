/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_overlay.h"

#include "base/event_filter.h"
#include "mwtk/mwtk_options.h"
#include "ui/style/style_core_scale.h"
#include "ui/ui_utility.h"

#include <QtGui/QPainter>

namespace Mwtk {
namespace {

constexpr auto kFrameDelay = crl::time(33);

[[nodiscard]] double CurrentScale() {
	return style::Scale() / 100.;
}

} // namespace

VisualsWidget::VisualsWidget(QWidget *parent, bool followOptions)
: RpWidget(parent)
, _timer([=] { tick(); })
, _followOptions(followOptions) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);

	sizeValue(
	) | rpl::on_next([=](QSize size) {
		_scene.resize(size);
		update();
	}, lifetime());

	if (_followOptions) {
		ConfigChanges(
		) | rpl::on_next([=] {
			refreshFromOptions();
		}, lifetime());
		refreshFromOptions();
	}
}

void VisualsWidget::setConfig(const SceneConfig &config) {
	_scene.setConfig(config);
	update();
}

void VisualsWidget::setRunning(bool running) {
	if (_running == running) {
		return;
	}
	_running = running;
	setVisible(running);
	if (running) {
		_lastTick = crl::now();
		_timer.callEach(kFrameDelay);
	} else {
		_timer.cancel();
	}
}

void VisualsWidget::refreshFromOptions() {
	setConfig(CurrentConfig(CurrentScale()));
	setRunning(VisualsEnabled());
}

bool VisualsWidget::canAnimate() const {
	const auto top = window();
	return isVisible()
		&& top
		&& top->isVisible()
		&& !(top->windowState() & Qt::WindowMinimized);
}

void VisualsWidget::tick() {
	const auto now = crl::now();
	const auto elapsed = now - _lastTick;
	_lastTick = now;
	if (!canAnimate()) {
		return;
	}
	_scene.advance(elapsed / 1000.);
	const auto dirty = _scene.takeDirtyRegion();
	if (!dirty.isEmpty()) {
		update(dirty);
	}
}

void VisualsWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_scene.paint(p);
}

void SetupOverlay(not_null<Ui::RpWidget*> body) {
	const auto overlay = Ui::CreateChild<VisualsWidget>(body.get(), true);
	body->sizeValue(
	) | rpl::on_next([=](QSize size) {
		overlay->setGeometry(QRect(QPoint(), size));
		overlay->raise();
	}, overlay->lifetime());
	base::install_event_filter(overlay, body, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ChildAdded) {
			crl::on_main(overlay, [=] {
				overlay->raise();
			});
		}
		return base::EventFilterResult::Continue;
	});
	overlay->raise();
}

} // namespace Mwtk
