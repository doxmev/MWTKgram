/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_room.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "mwtk/mwtk_options.h"
#include "mwtk/mwtk_scene.h"
#include "ui/style/style_core_scale.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QCursor>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <cmath>

namespace Mwtk {
namespace {

constexpr auto kTickDelay = crl::time(33);
constexpr auto kResizeRenderDelay = crl::time(250);
constexpr auto kParamsRenderDelay = crl::time(60);

class RoomController final {
public:
	RoomController();

	[[nodiscard]] static RoomController &Instance();

	[[nodiscard]] RoomType type() const;
	bool paint(QPainter &p, QSize fill);
	[[nodiscard]] rpl::producer<> repaints() const;
	[[nodiscard]] QPointF shift() const;
	[[nodiscard]] double time() const;

private:
	[[nodiscard]] RoomParams currentParams() const;
	void refresh();
	void tick();
	void wake();
	void render(QSize size);

	RoomType _type = RoomType::None;
	base::Timer _timer;
	base::Timer _renderTimer;
	rpl::event_stream<> _repaints;
	RoomFrame _frame;
	RoomParams _frameParams;
	QSize _wantedSize;
	QPointF _shift;
	double _time = 0.;
	crl::time _lastTick = 0;
	crl::time _lastRender = 0;
	rpl::lifetime _lifetime;

};

RoomController::RoomController()
: _timer([=] { tick(); })
, _renderTimer([=] { render(_wantedSize); }) {
	RoomConfigChanges(
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
	base::install_event_filter(
		QCoreApplication::instance(),
		[=](not_null<QEvent*> e) {
			switch (e->type()) {
			case QEvent::MouseMove:
			case QEvent::WindowActivate:
			case QEvent::ApplicationStateChange:
				wake();
				break;
			case QEvent::KeyPress: {
				const auto key = static_cast<QKeyEvent*>(e.get());
				if (key->key() == Qt::Key_F9
					&& !key->isAutoRepeat()
					&& !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
					crl::on_main([] { ToggleEffects(); });
					return base::EventFilterResult::Cancel;
				}
			} break;
			default:
				break;
			}
			return base::EventFilterResult::Continue;
		},
		_lifetime);
	refresh();
}

void RoomController::wake() {
	if (_type == RoomType::None || _timer.isActive()) {
		return;
	}
	if (!OptionValue(kOptionRoomParallax) && !OptionValue(kOptionRoomMotion)) {
		return;
	}
	_lastTick = crl::now();
	_timer.callEach(kTickDelay);
}

RoomController &RoomController::Instance() {
	static const auto result = new RoomController();
	return *result;
}

RoomType RoomController::type() const {
	return _type;
}

RoomParams RoomController::currentParams() const {
	auto result = RoomParams();
	result.type = _type;
	result.yaw = OptionInt(kOptionRoomYaw);
	result.pitch = OptionInt(kOptionRoomPitch);
	result.fov = OptionInt(kOptionRoomFov);
	result.ratio = style::DevicePixelRatio();
	const auto quality = CurrentQuality();
	result.scale = quality.roomScale;
	result.trees = quality.trees;
	result.accent = PaletteAt(OptionInt(kOptionPalette), 0.).first;
	return result;
}

void RoomController::refresh() {
	const auto value = EffectsEnabled() ? OptionInt(kOptionRoom) : 0;
	_type = (value == 1)
		? RoomType::Forest
		: (value == 2)
		? RoomType::Neon
		: RoomType::None;
	if (_type == RoomType::None) {
		_timer.cancel();
		_frame = RoomFrame();
	} else {
		wake();
		if (!_wantedSize.isEmpty()) {
			_renderTimer.callOnce(kParamsRenderDelay);
		}
	}
	_repaints.fire({});
}

void RoomController::render(QSize size) {
	if (_type == RoomType::None || size.isEmpty()) {
		return;
	}
	_frameParams = currentParams();
	_frame = RenderRoom(_frameParams, size);
	_lastRender = crl::now();
	_repaints.fire({});
}

bool RoomController::paint(QPainter &p, QSize fill) {
	if (_type == RoomType::None || fill.isEmpty()) {
		return false;
	}
	_wantedSize = fill;
	if (_frame.layers.empty()) {
		render(fill);
	} else if (_frame.size != fill || !(_frameParams == currentParams())) {
		if (!_renderTimer.isActive()) {
			_renderTimer.callOnce((_frame.size != fill)
				? kResizeRenderDelay
				: kParamsRenderDelay);
		}
	}
	PaintRoomFrame(
		p,
		_frame,
		fill,
		_shift,
		_time,
		OptionValue(kOptionRoomMotion));
	return true;
}

void RoomController::tick() {
	const auto now = crl::now();
	const auto dt = std::min((now - _lastTick) / 1000., 0.1);
	_lastTick = now;
	const auto window = QApplication::activeWindow();
	if (!window) {
		_timer.cancel();
		return;
	}
	const auto motion = OptionValue(kOptionRoomMotion);
	_time += dt;
	auto target = QPointF();
	if (OptionValue(kOptionRoomParallax) && window->width() > 0) {
		const auto local = window->mapFromGlobal(QCursor::pos());
		const auto nx = std::clamp(local.x() * 2. / window->width() - 1., -1., 1.);
		const auto ny = std::clamp(local.y() * 2. / std::max(window->height(), 1) - 1., -1., 1.);
		target = QPointF(nx * 0.32, ny * 0.18);
	}
	if (motion) {
		target += QPointF(
			0.07 * std::sin(_time * 0.23),
			0.04 * std::sin(_time * 0.31 + 0.8));
	}
	const auto k = 1. - std::exp(-dt * 2.6);
	const auto was = _shift;
	_shift += (target - _shift) * k;
	const auto moved = std::abs(_shift.x() - was.x())
		+ std::abs(_shift.y() - was.y());
	if (motion || moved > 0.0004) {
		_repaints.fire({});
	} else {
		_timer.cancel();
	}
}

rpl::producer<> RoomController::repaints() const {
	return _repaints.events();
}

QPointF RoomController::shift() const {
	return _shift;
}

double RoomController::time() const {
	return _time;
}

} // namespace

RoomType CurrentRoom() {
	return RoomController::Instance().type();
}

bool PaintRoom(QPainter &p, QSize fill) {
	return RoomController::Instance().paint(p, fill);
}

rpl::producer<> RoomRepaintRequests() {
	return RoomController::Instance().repaints();
}

QPointF RoomCameraShift() {
	return RoomController::Instance().shift();
}

double RoomTime() {
	return RoomController::Instance().time();
}

} // namespace Mwtk
