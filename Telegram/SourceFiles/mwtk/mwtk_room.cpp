/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_room.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "media/clip/media_clip_reader.h"
#include "mwtk/mwtk_options.h"
#include "mwtk/mwtk_scene.h"
#include "mwtk/mwtk_sounds.h"
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
	bool paint(QPainter &p, QSize fill, bool primary);
	[[nodiscard]] rpl::producer<> repaints() const;
	[[nodiscard]] QPointF shift() const;
	[[nodiscard]] double time() const;
	[[nodiscard]] RoomParams currentParams() const;
	bool paintVideo(QPainter &p, QSize fill);

private:
	void refresh();
	void refreshVideo();
	void videoCallback(::Media::Clip::Notification notification);
	void scheduleVideoRepaint();
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
	::Media::Clip::ReaderPointer _video;
	QString _videoPath;
	QSize _videoRequest;
	base::Timer _videoTimer;
	base::Timer _clockTimer;
	bool _night = false;
	crl::time _videoLastRepaint = 0;
	rpl::lifetime _lifetime;

};

RoomController::RoomController()
: _timer([=] { tick(); })
, _renderTimer([=] { render(_wantedSize); })
, _videoTimer([=] {
	_videoLastRepaint = crl::now();
	_repaints.fire({});
})
, _clockTimer([=] {
	const auto night = NightEventActive();
	if (_night != night) {
		_night = night;
		_repaints.fire({});
	}
}) {
	_night = NightEventActive();
	RoomConfigChanges(
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
	base::install_event_filter(
		QCoreApplication::instance(),
		[=](not_null<QEvent*> e) {
			switch (e->type()) {
			case QEvent::MouseMove:
				wake();
				break;
			case QEvent::WindowActivate:
			case QEvent::ApplicationStateChange:
				wake();
				if (_type == RoomType::Video) {
					_repaints.fire({});
				}
				break;
			case QEvent::KeyPress: {
				const auto key = static_cast<QKeyEvent*>(e.get());
				if (key->key() == Qt::Key_F9
					&& !key->isAutoRepeat()
					&& !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
					crl::on_main([] {
						const auto was = EffectsEnabled();
						if (was) {
							Sounds::Play(Sounds::Sound::Toggle);
						}
						ToggleEffects();
						if (!was) {
							Sounds::Play(Sounds::Sound::Toggle);
						}
					});
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
	// The whole window repaints on each tick, so follow the quality preset
	// (and the laptop mode) instead of a fixed 30 fps.
	_timer.callEach(std::max(kTickDelay, crl::time(CurrentQuality().frameDelay)));
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
	result.night = NightEventActive();
	if (_type == RoomType::Custom) {
		result.base = SceneBase(
			std::clamp(OptionInt(kOptionSceneBase), 0, kSceneBaseCount - 1));
		result.timeOfDay = std::clamp(
			OptionInt(kOptionSceneTime),
			0,
			kSceneTimeCount - 1);
		result.density = std::clamp(OptionInt(kOptionSceneDensity), 0, 100);
		result.fog = std::clamp(OptionInt(kOptionSceneFog), 0, 100);
		result.notes = OptionString(kOptionSceneNotes);
	} else if (_type == RoomType::Photo) {
		result.photo = OptionString(kOptionBackgroundPhoto);
		result.dim = std::clamp(OptionInt(kOptionBackgroundDim), 0, 90);
	} else if (_type == RoomType::Video) {
		result.dim = std::clamp(OptionInt(kOptionBackgroundDim), 0, 90);
	}
	return result;
}

void RoomController::refresh() {
	// Nothing ticks while MWTK effects are off.
	if (EffectsEnabled() && OptionValue(kOptionNightEvents)) {
		if (!_clockTimer.isActive()) {
			_clockTimer.callEach(60 * crl::time(1000));
		}
	} else {
		_clockTimer.cancel();
	}
	const auto value = EffectsEnabled() ? OptionInt(kOptionRoom) : 0;
	_type = (value >= 1 && value < kBackgroundCount)
		? RoomType(value)
		: RoomType::None;
	refreshVideo();
	if (_type == RoomType::None) {
		_timer.cancel();
		_frame = RoomFrame();
	} else {
		_timer.cancel();
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

bool RoomController::paint(QPainter &p, QSize fill, bool primary) {
	if (_type == RoomType::None || fill.isEmpty()) {
		return false;
	}
	if (primary) {
		_wantedSize = fill;
	} else if (_wantedSize.isEmpty()) {
		_wantedSize = fill;
	}
	if (_frame.size.isEmpty()) {
		render(_wantedSize);
	} else if (_frame.size != _wantedSize
		|| !(_frameParams == currentParams())) {
		if (!_renderTimer.isActive()) {
			_renderTimer.callOnce((_frame.size != _wantedSize)
				? kResizeRenderDelay
				: kParamsRenderDelay);
		}
	}
	if (_type == RoomType::Video) {
		paintVideo(p, fill);
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

void RoomController::refreshVideo() {
	const auto path = (_type == RoomType::Video)
		? OptionString(kOptionBackgroundVideo)
		: QString();
	if (path == _videoPath) {
		return;
	}
	_videoPath = path;
	_videoRequest = QSize();
	_videoTimer.cancel();
	_video.reset();
	if (!path.isEmpty()) {
		_video = ::Media::Clip::MakeReader(path, [=](
				::Media::Clip::Notification notification) {
			videoCallback(notification);
		});
	}
}

void RoomController::videoCallback(
		::Media::Clip::Notification notification) {
	using namespace ::Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		if (_video && _video->state() == State::Error) {
			_video.setBad();
		}
		if (_video && _video->ready() && !_video->started()) {
			const auto quality = std::clamp(OptionInt(kOptionQuality), 0, 2);
			const auto maxSide = (quality == 0)
				? 640
				: (quality == 1)
				? 960
				: 1280;
			const auto original = QSize(
				std::max(_video->width(), 1),
				std::max(_video->height(), 1));
			const auto pixels = (original.width() > maxSide
				|| original.height() > maxSide)
				? original.scaled(maxSide, maxSide, Qt::KeepAspectRatio)
				: original;
			const auto ratio = style::DevicePixelRatio();
			_videoRequest = QSize(
				std::max(pixels.width() / ratio, 1),
				std::max(pixels.height() / ratio, 1));
			_video->start({ .frame = _videoRequest });
		}
		_repaints.fire({});
	} break;
	case Notification::Repaint: {
		if (_video && !_video->currentDisplayed()) {
			scheduleVideoRepaint();
		}
	} break;
	}
}

void RoomController::scheduleVideoRepaint() {
	if (_videoTimer.isActive()) {
		return;
	}
	const auto delay = std::max(
		crl::time(CurrentQuality().frameDelay),
		crl::time(33));
	const auto elapsed = crl::now() - _videoLastRepaint;
	if (elapsed >= delay) {
		_videoLastRepaint = crl::now();
		_repaints.fire({});
	} else {
		_videoTimer.callOnce(delay - elapsed);
	}
}

bool RoomController::paintVideo(QPainter &p, QSize fill) {
	if (fill.isEmpty()) {
		return false;
	}
	if (!_video || !_video->started() || _videoRequest.isEmpty()) {
		p.fillRect(QRect(QPoint(), fill), QColor(12, 10, 18));
		if (_videoPath.isEmpty() || (!_video && !_video.isBad())) {
			return false;
		}
		p.setPen(QColor(255, 255, 255, 120));
		p.drawText(
			QRect(QPoint(), fill),
			Qt::AlignCenter,
			_video.isBad()
				? QString::fromUtf8("Не удалось открыть видео")
				: QString::fromUtf8("Загрузка видео…"));
		return false;
	}
	const auto active = (QApplication::activeWindow() != nullptr)
		|| OptionValue(kOptionBackgroundAnimation);
	const auto frame = _video->current(
		{ .frame = _videoRequest },
		active ? crl::now() : 0);
	if (frame.isNull()) {
		return false;
	}
	constexpr auto kExtra = 0.05;
	const auto cover = QSizeF(_videoRequest).scaled(
		QSizeF(fill) * (1. + 2. * kExtra),
		Qt::KeepAspectRatioByExpanding);
	const auto offset = QPointF(
		std::clamp(-_shift.x() / 0.32, -1., 1.) * fill.width() * kExtra,
		std::clamp(-_shift.y() / 0.18, -1., 1.) * fill.height() * kExtra);
	const auto target = QRectF(
		(fill.width() - cover.width()) / 2. + offset.x(),
		(fill.height() - cover.height()) / 2. + offset.y(),
		cover.width(),
		cover.height());
	p.save();
	p.setRenderHint(
		QPainter::SmoothPixmapTransform,
		CurrentQuality().decorativeMotion);
	p.drawImage(target, frame);
	p.restore();
	const auto dim = std::clamp(OptionInt(kOptionBackgroundDim), 0, 90);
	if (dim > 0) {
		p.fillRect(QRect(QPoint(), fill), QColor(0, 0, 0, dim * 255 / 100));
	}
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
	return RoomController::Instance().paint(p, fill, false);
}

bool PaintRoomInWindow(QPainter &p, not_null<QWidget*> widget) {
	const auto window = widget->window();
	if (!window || CurrentRoom() == RoomType::None) {
		return false;
	}
	const auto position = widget->mapTo(window, QPoint());
	p.save();
	p.translate(-position);
	const auto result = RoomController::Instance().paint(
		p,
		window->size(),
		true);
	p.restore();
	return result;
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

RoomParams CurrentRoomParams() {
	return RoomController::Instance().currentParams();
}

bool PaintRoomVideo(QPainter &p, QSize fill) {
	return RoomController::Instance().paintVideo(p, fill);
}

} // namespace Mwtk
