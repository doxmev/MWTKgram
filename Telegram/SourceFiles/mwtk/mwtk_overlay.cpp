/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_overlay.h"

#include "base/event_filter.h"
#include "mwtk/mwtk_options.h"
#include "mwtk/mwtk_room.h"
#include "mwtk/mwtk_appear.h"
#include "mwtk/mwtk_floating.h"
#include "mwtk/mwtk_hidden.h"
#include "mwtk/mwtk_sounds.h"
#include "mwtk/mwtk_style.h"
#include "core/application.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/style/style_core_scale.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <cmath>
#include <random>

namespace Mwtk {
namespace {

[[nodiscard]] bool IsForestRoom(RoomType room) {
	return (room == RoomType::Forest)
		|| (room == RoomType::Custom
			&& OptionInt(kOptionSceneBase) == int(SceneBase::Forest));
}

constexpr auto kButtonGlow = 12;

[[nodiscard]] double CurrentScale() {
	return style::Scale() / 100.;
}

[[nodiscard]] double Ease(double dt, double speed) {
	return 1. - std::exp(-dt * speed);
}

[[nodiscard]] QColor WithAlpha(QColor color, double alpha) {
	color.setAlphaF(float(std::clamp(alpha, 0., 1.)));
	return color;
}

[[nodiscard]] QRectF Lerp(const QRectF &a, const QRectF &b, double k) {
	return QRectF(
		a.x() + (b.x() - a.x()) * k,
		a.y() + (b.y() - a.y()) * k,
		a.width() + (b.width() - a.width()) * k,
		a.height() + (b.height() - a.height()) * k);
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
		refreshFrameCache();
		update();
	}, lifetime());

	if (_followOptions) {
		rpl::merge(
			ConfigChanges(),
			InterfaceConfigChanges()
		) | rpl::on_next([=] {
			refreshFromOptions();
		}, lifetime());
		RoomRepaintRequests(
		) | rpl::on_next([=] {
			if (_frameEnabled) {
				wake();
			}
		}, lifetime());
		base::install_event_filter(
			this,
			QCoreApplication::instance(),
			[=](not_null<QEvent*> e) {
				switch (e->type()) {
				case QEvent::MouseMove:
					if (_running && _trailEnabled) {
						pushTrailPoint();
					}
					if (_running) {
						wake();
					}
					break;
				case QEvent::MouseButtonPress:
				case QEvent::MouseButtonRelease:
				case QEvent::Enter:
				case QEvent::Leave:
				case QEvent::WindowActivate:
				case QEvent::WindowDeactivate:
				case QEvent::ApplicationStateChange:
					if (_running) {
						wake();
					}
					break;
				default:
					break;
				}
				return base::EventFilterResult::Continue;
			});
		refreshFromOptions();
	} else {
		_sceneEnabled = true;
		_frameDelay = CurrentQuality().frameDelay;
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
		wake();
	} else {
		_timer.cancel();
	}
}

void VisualsWidget::wake() {
	if (!_running || _timer.isActive()) {
		return;
	}
	_lastTick = crl::now();
	_timer.callEach(_frameDelay);
}

bool VisualsWidget::sceneAllowed() const {
	if (!_sceneEnabled) {
		return false;
	}
	if (!_followOptions || OptionValue(kOptionBackgroundAnimation)) {
		return true;
	}
	const auto top = window();
	return top && (QApplication::activeWindow() == top);
}

bool VisualsWidget::buttonsSettled() const {
	const auto &state = _button;
	if (!state.ripples.empty()) {
		return false;
	}
	const auto goalAlpha = state.target ? 1. : 0.;
	if (std::abs(state.alpha - goalAlpha) > 0.004) {
		return false;
	}
	const auto goalPress = state.pressed ? 1. : 0.;
	if (std::abs(state.press - goalPress) > 0.004) {
		return false;
	}
	if (state.target && state.alpha > 0.004) {
		const auto goal = QRectF(QRect(
			state.target->mapTo(parentWidget(), QPoint()),
			state.target->size()));
		const auto d = std::abs(goal.x() - state.rect.x())
			+ std::abs(goal.y() - state.rect.y())
			+ std::abs(goal.width() - state.rect.width())
			+ std::abs(goal.height() - state.rect.height());
		if (d > 0.5) {
			return false;
		}
	}
	return true;
}

void VisualsWidget::refreshFromOptions() {
	const auto quality = CurrentQuality();
	if (_frameDelay != quality.frameDelay) {
		_frameDelay = quality.frameDelay;
		_timer.cancel();
	}
	_frameAnimated = quality.decorativeMotion;
	setConfig(CurrentConfig(CurrentScale()));
	const auto effects = EffectsEnabled();
	_sceneEnabled = VisualsEnabled();
	_buttons3d = effects && OptionValue(kOptionButtons3d);
	_buttonsAnimated = effects && OptionValue(kOptionButtonsAnimated);
	_frameEnabled = effects && OptionValue(kOptionUiScene);
	_trailEnabled = effects && OptionValue(kOptionCursorTrail);
	_burstEnabled = effects && OptionValue(kOptionSendEffect);
	if (!_trailEnabled) {
		_trail.clear();
		if (!_trailDirty.isEmpty()) {
			update(_trailDirty);
			_trailDirty = QRect();
		}
	}
	refreshFrameCache();
	setRunning(_sceneEnabled
		|| _buttons3d
		|| _buttonsAnimated
		|| _frameEnabled
		|| _trailEnabled
		|| _burstEnabled);
	wake();
	update();
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
	const auto dt = std::min((now - _lastTick) / 1000., 0.1);
	_lastTick = now;
	if (!canAnimate()) {
		_timer.cancel();
		return;
	}
	_frameTime += dt;
	auto dirty = QRegion();
	const auto scene = sceneAllowed();
	if (scene) {
		_scene.advance(dt);
		dirty += _scene.takeDirtyRegion();
	}
	auto frameMoving = false;
	if (_followOptions) {
		dirty += tickButtons(dt);
		dirty += tickTrail();
		dirty += tickBurst(dt);
		const auto frame = tickFrame(dt);
		frameMoving = !frame.isEmpty();
		dirty += frame;
	}
	if (!dirty.isEmpty()) {
		update(dirty);
	}
	if (!scene
		&& !frameMoving
		&& buttonsSettled()
		&& _trail.empty()
		&& _sparks.empty()
		&& _shocks.empty()) {
		_timer.cancel();
	}
}

void VisualsWidget::burst(QPointF at) {
	if (!_burstEnabled || !_running) {
		return;
	}
	static auto random = std::mt19937(777);
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	const auto scale = CurrentScale();
	constexpr auto kPi = 3.14159265358979323846;
	for (auto i = 0; i != 26; ++i) {
		const auto angle = (i < 18)
			? (-kPi * (0.1 + 0.8 * unit(random)))
			: (unit(random) * 2. * kPi);
		const auto speed = (140. + 280. * unit(random)) * scale;
		_sparks.push_back({
			.position = at,
			.velocity = QPointF(std::cos(angle), std::sin(angle)) * speed,
			.life = 0.75 + 0.25 * unit(random),
			.size = (1.8 + 2.6 * unit(random)) * scale,
			.color = i % 3,
		});
	}
	if (_sparks.size() > 200) {
		_sparks.erase(_sparks.begin(), _sparks.begin() + (_sparks.size() - 200));
	}
	_shocks.push_back({ at, 0. });
	wake();
}

QRegion VisualsWidget::tickBurst(double dt) {
	if (_sparks.empty() && _shocks.empty() && _burstDirty.isEmpty()) {
		return QRegion();
	}
	const auto scale = CurrentScale();
	const auto drag = std::exp(-dt * 1.8);
	for (auto &spark : _sparks) {
		spark.life -= dt / 0.9;
		spark.velocity *= drag;
		spark.velocity.ry() += 520. * scale * dt;
		spark.position += spark.velocity * dt;
	}
	_sparks.erase(
		std::remove_if(_sparks.begin(), _sparks.end(), [](const Spark &spark) {
			return spark.life <= 0.;
		}),
		_sparks.end());
	for (auto &shock : _shocks) {
		shock.progress += dt / 0.45;
	}
	_shocks.erase(
		std::remove_if(_shocks.begin(), _shocks.end(), [](const Shock &shock) {
			return shock.progress >= 1.;
		}),
		_shocks.end());
	auto bounds = QRectF();
	for (const auto &spark : _sparks) {
		const auto r = spark.size * 3.5;
		bounds = bounds.united(QRectF(
			spark.position - QPointF(r, r),
			QSizeF(2 * r, 2 * r)));
	}
	for (const auto &shock : _shocks) {
		const auto r = 50. * scale + 4.;
		bounds = bounds.united(QRectF(
			shock.center - QPointF(r, r),
			QSizeF(2 * r, 2 * r)));
	}
	const auto now = bounds.isEmpty()
		? QRect()
		: bounds.toAlignedRect().marginsAdded({ 2, 2, 2, 2 });
	auto result = QRegion(_burstDirty);
	if (!now.isEmpty()) {
		result += now;
	}
	_burstDirty = now;
	return result;
}

void VisualsWidget::paintBurst(QPainter &p) const {
	if (_sparks.empty() && _shocks.empty()) {
		return;
	}
	const auto palette = PaletteAt(OptionInt(kOptionPalette), _frameTime);
	const QColor colors[] = { palette.first, palette.second, palette.third };
	const auto scale = CurrentScale();
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	for (const auto &shock : _shocks) {
		const auto radius = (8. + 42. * shock.progress) * scale;
		const auto alpha = 0.7 * (1. - shock.progress);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(WithAlpha(palette.first, alpha), 2.5 * scale * (1. - shock.progress) + 0.5));
		p.drawEllipse(shock.center, radius, radius);
	}
	p.setPen(Qt::NoPen);
	for (const auto &spark : _sparks) {
		const auto life = std::clamp(spark.life, 0., 1.);
		const auto &color = colors[spark.color];
		p.setBrush(WithAlpha(color, 0.22 * life));
		p.drawEllipse(spark.position, spark.size * 3., spark.size * 3.);
		p.setBrush(WithAlpha(color, 0.95 * life));
		p.drawEllipse(spark.position, spark.size, spark.size);
	}
	p.restore();
}

void VisualsWidget::pushTrailPoint() {
	const auto top = window();
	if (!top || QApplication::activeWindow() != top) {
		return;
	}
	const auto position = QPointF(mapFromGlobal(QCursor::pos()));
	if (!rect().contains(position.toPoint())) {
		return;
	}
	const auto now = crl::now();
	if (!_trail.empty()) {
		const auto delta = position - _trail.back().position;
		if (std::abs(delta.x()) + std::abs(delta.y()) < 2.) {
			_trail.back().at = now;
			return;
		}
	}
	_trail.push_back({ position, now });
	if (_trail.size() > 48) {
		_trail.erase(_trail.begin());
	}
}

QRegion VisualsWidget::tickTrail() {
	constexpr auto kLifetime = crl::time(320);
	const auto now = crl::now();
	_trail.erase(
		std::remove_if(_trail.begin(), _trail.end(), [&](const TrailPoint &point) {
			return now - point.at > kLifetime;
		}),
		_trail.end());
	auto bounds = QRectF();
	for (const auto &point : _trail) {
		bounds = bounds.united(QRectF(point.position, QSizeF(1., 1.)));
	}
	const auto margin = int(std::ceil(12. * CurrentScale()));
	const auto now_ = _trail.empty()
		? QRect()
		: bounds.toAlignedRect().marginsAdded({ margin, margin, margin, margin });
	auto result = QRegion(_trailDirty);
	if (!now_.isEmpty()) {
		result += now_;
	}
	_trailDirty = now_;
	return result;
}

void VisualsWidget::paintTrail(QPainter &p) const {
	if (!_trailEnabled || _trail.size() < 2) {
		return;
	}
	constexpr auto kLifetime = 320.;
	const auto now = crl::now();
	const auto palette = PaletteAt(OptionInt(kOptionPalette), _frameTime);
	const auto scale = CurrentScale();
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	const auto count = int(_trail.size());
	for (auto i = 1; i != count; ++i) {
		const auto &from = _trail[i - 1];
		const auto &till = _trail[i];
		const auto age = std::clamp((now - till.at) / kLifetime, 0., 1.);
		const auto order = double(i) / (count - 1);
		const auto life = (1. - age) * order;
		if (life <= 0.01) {
			continue;
		}
		const auto color = QColor::fromRgbF(
			float(palette.third.redF() * (1. - order) + palette.first.redF() * order),
			float(palette.third.greenF() * (1. - order) + palette.first.greenF() * order),
			float(palette.third.blueF() * (1. - order) + palette.first.blueF() * order));
		const auto width = (1. + 4. * life) * scale;
		p.setPen(QPen(WithAlpha(color, 0.22 * life), width * 3.2, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(from.position, till.position);
		p.setPen(QPen(WithAlpha(color, 0.9 * life), width, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(from.position, till.position);
	}
	p.restore();
}

QWidget *VisualsWidget::findHoveredButton() const {
	const auto top = window();
	if (!top || QApplication::activeWindow() != top) {
		return nullptr;
	}
	const auto parent = parentWidget();
	auto widget = QApplication::widgetAt(QCursor::pos());
	while (widget && widget != top) {
		if (dynamic_cast<Ui::AbstractButton*>(widget)) {
			break;
		}
		widget = widget->parentWidget();
	}
	if (!widget
		|| widget == top
		|| !parent
		|| !parent->isAncestorOf(widget)
		|| !widget->isVisible()) {
		return nullptr;
	}
	const auto area = double(widget->width()) * widget->height();
	const auto full = double(std::max(1, width())) * std::max(1, height());
	if (widget->height() > 180 * CurrentScale() || area > full * 0.3) {
		return nullptr;
	}
	return widget;
}

QRegion VisualsWidget::tickButtons(double dt) {
	auto &state = _button;
	if (!_buttons3d && !_buttonsAnimated) {
		if (!state.lastDirty.isEmpty()) {
			const auto result = QRegion(state.lastDirty);
			state = ButtonState();
			return result;
		}
		return QRegion();
	}
	const auto cursor = QCursor::pos();
	if (cursor != _lastCursor || (_lastHovered && !_lastHovered->isVisible())) {
		_lastCursor = cursor;
		_lastHovered = findHoveredButton();
	}
	const auto hovered = _lastHovered.data();
	if (hovered) {
		const auto goal = QRectF(
			QRect(hovered->mapTo(parentWidget(), QPoint()), hovered->size())
		).intersected(QRectF(rect()));
		if (state.target != hovered) {
			if (state.alpha < 0.05 || state.rect.isEmpty()) {
				state.rect = goal;
			}
			state.target = hovered;
		}
		state.rect = Lerp(state.rect, goal, Ease(dt, 16.));
		state.alpha += (1. - state.alpha) * Ease(dt, 10.);
		const auto local = mapFromGlobal(QCursor::pos());
		state.light = QPointF(
			std::clamp((local.x() - state.rect.x()) / std::max(1., state.rect.width()), 0., 1.),
			std::clamp((local.y() - state.rect.y()) / std::max(1., state.rect.height()), 0., 1.));
		const auto pressed = (QGuiApplication::mouseButtons() & Qt::LeftButton) != 0;
		if (pressed && !state.pressed && _buttonsAnimated) {
			state.ripples.push_back({ QPointF(local), 0. });
		}
		state.pressed = pressed;
	} else {
		state.alpha += (0. - state.alpha) * Ease(dt, 8.);
		state.pressed = false;
		if (state.alpha < 0.01) {
			state.target = nullptr;
		}
	}
	state.press += ((state.pressed ? 1. : 0.) - state.press) * Ease(dt, 22.);
	for (auto &ripple : state.ripples) {
		ripple.progress += dt / 0.65;
	}
	state.ripples.erase(
		std::remove_if(state.ripples.begin(), state.ripples.end(), [](const Ripple &r) {
			return r.progress >= 1.;
		}),
		state.ripples.end());

	const auto margin = int(kButtonGlow * CurrentScale()) + 4;
	auto now = (state.alpha > 0.003)
		? state.rect.toAlignedRect().marginsAdded({ margin, margin, margin, margin })
		: QRect();
	for (const auto &ripple : state.ripples) {
		const auto r = int(std::ceil(std::max(state.rect.width(), state.rect.height()) * 1.2));
		now = now.united(QRect(
			int(ripple.center.x()) - r,
			int(ripple.center.y()) - r,
			2 * r,
			2 * r));
	}
	auto result = QRegion(state.lastDirty);
	if (!now.isEmpty()) {
		result += now;
	}
	state.lastDirty = now;
	return result;
}

void VisualsWidget::refreshFrameCache() {
	if (!_frameEnabled || size().isEmpty()) {
		_branch = QImage();
		_branchFor = QSize();
		return;
	}
	const auto wanted = QSize(
		std::max(80, int(width() * 0.3)),
		std::max(60, int(height() * 0.3)));
	if (_branchFor != wanted) {
		_branchFor = wanted;
		_branch = RenderBranch(wanted, style::DevicePixelRatio());
	}
}

QRegion VisualsWidget::tickFrame(double) {
	if (!_frameEnabled) {
		if (!_frameRects.empty()) {
			auto result = QRegion();
			for (const auto &rect : _frameRects) {
				result += rect;
			}
			_frameRects.clear();
			return result;
		}
		return QRegion();
	}
	const auto was = _frameShift;
	_frameShift = RoomCameraShift();
	const auto moved = std::abs(was.x() - _frameShift.x())
		+ std::abs(was.y() - _frameShift.y());
	const auto room = CurrentRoom();
	const auto animated = _frameAnimated
		&& ((room == RoomType::Neon) || OptionValue(kOptionRoomMotion));
	if (moved < 0.0003 && !animated && !_frameRects.empty()) {
		return QRegion();
	}
	auto rects = std::vector<QRect>();
	const auto w = width();
	const auto h = height();
	const auto pad = int(std::max(w, h) * 0.06);
	if (IsForestRoom(room) && !_branch.isNull()) {
		const auto bw = _branchFor.width() + pad * 2;
		const auto bh = _branchFor.height() + pad * 2;
		rects.push_back(QRect(-pad, -pad, bw, bh));
		rects.push_back(QRect(w - bw + pad, -pad, bw, bh));
		rects.push_back(QRect(0, h - int(h * 0.16), w, int(h * 0.16)));
	} else {
		const auto edge = int(std::max(18., 26. * CurrentScale()));
		rects.push_back(QRect(0, 0, w, edge));
		rects.push_back(QRect(0, h - edge, w, edge));
		rects.push_back(QRect(0, 0, edge, h));
		rects.push_back(QRect(w - edge, 0, edge, h));
	}
	auto result = QRegion();
	for (const auto &rect : _frameRects) {
		result += rect;
	}
	for (const auto &rect : rects) {
		result += rect;
	}
	_frameRects = std::move(rects);
	return result;
}

void VisualsWidget::paintFrame(QPainter &p) const {
	if (!_frameEnabled) {
		return;
	}
	const auto w = double(width());
	const auto h = double(height());
	const auto room = CurrentRoom();
	const auto palette = PaletteAt(OptionInt(kOptionPalette), _frameTime);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	if (IsForestRoom(room) && !_branch.isNull()) {
		const auto shift = QPointF(
			-_frameShift.x() * w * 0.12,
			_frameShift.y() * h * 0.12);
		const auto sway = _frameAnimated
			? std::sin(_frameTime * 0.6) * 2.
			: 0.;
		const auto size = QSizeF(_branchFor);
		p.setOpacity(0.95);
		p.save();
		p.translate(shift.x(), shift.y());
		p.rotate(sway * 0.4);
		p.drawImage(QRectF(QPointF(), size), _branch);
		p.restore();
		p.save();
		p.translate(w + shift.x(), shift.y());
		p.rotate(-sway * 0.4);
		p.scale(-1., 1.);
		p.drawImage(QRectF(QPointF(), size), _branch);
		p.restore();
		p.setOpacity(1.);
		auto mist = QLinearGradient(0, h * 0.84, 0, h);
		const auto fog = QColor(40, 54, 60);
		mist.setColorAt(0., WithAlpha(fog, 0.));
		mist.setColorAt(1., WithAlpha(fog, 0.35 + 0.06 * std::sin(_frameTime * 0.5)));
		p.fillRect(QRectF(0, h * 0.84, w, h * 0.16), mist);
	} else {
		const auto edge = std::max(18., 26. * CurrentScale());
		const auto pulse = _frameAnimated
			? (0.65 + 0.35 * std::sin(_frameTime * 1.4))
			: 0.8;
		const auto glow = (room == RoomType::Neon)
			? QColor(255, 47, 208)
			: palette.first;
		const auto cool = (room == RoomType::Neon)
			? QColor(34, 224, 255)
			: palette.third;
		const auto drawEdge = [&](QRectF rect, QPointF from, QPointF till) {
			auto gradient = QLinearGradient(from, till);
			gradient.setColorAt(0., WithAlpha(glow, 0.28 * pulse));
			gradient.setColorAt(1., WithAlpha(glow, 0.));
			p.fillRect(rect, gradient);
		};
		drawEdge(QRectF(0, 0, w, edge), { 0, 0 }, { 0, edge });
		drawEdge(QRectF(0, h - edge, w, edge), { 0, h }, { 0, h - edge });
		drawEdge(QRectF(0, 0, edge, h), { 0, 0 }, { edge, 0 });
		drawEdge(QRectF(w - edge, 0, edge, h), { w, 0 }, { w - edge, 0 });
		auto line = QLinearGradient(0, 0, w, h);
		const auto phase = std::fmod(_frameTime * 0.1, 1.);
		line.setColorAt(0., WithAlpha(glow, 0.9));
		line.setColorAt(std::clamp(phase, 0.01, 0.99), WithAlpha(cool, 0.9));
		line.setColorAt(1., WithAlpha(glow, 0.9));
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(line), 1.6 * CurrentScale()));
		const auto inset = 2. * CurrentScale();
		p.drawRoundedRect(QRectF(inset, inset, w - 2 * inset, h - 2 * inset), 8., 8.);
	}
	p.restore();
}

void VisualsWidget::paintButtons(QPainter &p) const {
	const auto &state = _button;
	if ((!_buttons3d && !_buttonsAnimated)
		|| (state.alpha <= 0.003 && state.ripples.empty())) {
		return;
	}
	const auto palette = PaletteAt(OptionInt(kOptionPalette), _frameTime);
	const auto rect = state.rect;
	const auto radius = std::min(10. * CurrentScale(), rect.height() / 3.);
	auto shape = QPainterPath();
	shape.addRoundedRect(rect, radius, radius);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	if (_buttons3d && state.alpha > 0.003) {
		const auto a = state.alpha;
		const auto press = state.press;
		const auto lift = (1. - press) * 3. * CurrentScale();
		auto shadow = QLinearGradient(rect.bottomLeft(), rect.bottomLeft() + QPointF(0, lift + 6.));
		shadow.setColorAt(0., QColor(0, 0, 0, int(70 * a)));
		shadow.setColorAt(1., QColor(0, 0, 0, 0));
		p.fillRect(QRectF(rect.left() + radius, rect.bottom(), rect.width() - 2 * radius, lift + 6.), shadow);

		p.save();
		p.setClipPath(shape);
		auto bevel = QLinearGradient(rect.topLeft(), rect.bottomLeft());
		const auto top = (1. - press) * 0.22 * a;
		const auto bottom = (0.18 + press * 0.1) * a;
		bevel.setColorAt(0., QColor(255, 255, 255, int(255 * top)));
		bevel.setColorAt(0.45, QColor(255, 255, 255, 0));
		bevel.setColorAt(0.55, QColor(0, 0, 0, 0));
		bevel.setColorAt(1., QColor(0, 0, 0, int(255 * bottom)));
		p.fillRect(rect, bevel);
		if (press > 0.01) {
			p.fillRect(rect, QColor(0, 0, 0, int(40 * press * a)));
		}
		const auto light = QPointF(
			rect.x() + rect.width() * state.light.x(),
			rect.y() + rect.height() * state.light.y());
		auto spot = QRadialGradient(light, std::max(rect.width(), rect.height()) * 0.6);
		spot.setColorAt(0., QColor(255, 255, 255, int(46 * a * (1. - press))));
		spot.setColorAt(1., QColor(255, 255, 255, 0));
		p.fillRect(rect, spot);
		p.restore();
	}
	if (_buttonsAnimated && state.alpha > 0.003) {
		auto gradient = QLinearGradient(rect.topLeft(), rect.topRight());
		const auto phase = std::fmod(_frameTime * 0.35, 1.);
		gradient.setColorAt(0., palette.first);
		gradient.setColorAt(std::clamp(phase, 0.01, 0.99), palette.second);
		gradient.setColorAt(1., palette.third);
		p.setBrush(Qt::NoBrush);
		for (auto i = 0; i != 4; ++i) {
			const auto widthPx = (kButtonGlow - i * 3.) * CurrentScale() * 0.6;
			p.setOpacity(state.alpha * (0.05 + i * 0.06));
			p.setPen(QPen(QBrush(gradient), std::max(1., widthPx)));
			p.drawPath(shape);
		}
		p.setOpacity(state.alpha);
		p.setPen(QPen(QBrush(gradient), 1.4 * CurrentScale()));
		p.drawPath(shape);
		p.setOpacity(1.);
	}
	for (const auto &ripple : state.ripples) {
		const auto r = std::max(rect.width(), rect.height()) * 1.1 * ripple.progress;
		const auto fade = 1. - ripple.progress;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(WithAlpha(palette.second, 0.7 * fade), 2. * CurrentScale() * (0.5 + fade)));
		p.drawEllipse(ripple.center, r, r);
	}
	p.restore();
}

void VisualsWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_sceneEnabled) {
		_scene.paint(p, e->region());
	}
	if (_followOptions) {
		paintFrame(p);
		paintButtons(p);
		paintTrail(p);
		paintBurst(p);
	}
}

void SetupOverlay(not_null<Ui::RpWidget*> body) {
	SetupChatStyle();
	const auto overlay = Ui::CreateChild<VisualsWidget>(body.get(), true);
	body->sizeValue(
	) | rpl::on_next([=](QSize size) {
		overlay->setGeometry(QRect(QPoint(), size));
		overlay->raise();
	}, overlay->lifetime());
	base::install_event_filter(overlay, body.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ChildAdded) {
			crl::on_main(overlay, [=] {
				overlay->raise();
			});
		}
		return base::EventFilterResult::Continue;
	});
	overlay->raise();

	Sounds::Setup();
	Hidden::Setup();
	Floating::Setup();
	Core::App().domain().activeSessionValue(
	) | rpl::map([](Main::Session *session) {
		return session
			? session->data().newItemAdded()
			: rpl::never<not_null<HistoryItem*>>();
	}) | rpl::flatten_latest(
	) | rpl::on_next([](not_null<HistoryItem*> item) {
		Appear::Register(item);
	}, overlay->lifetime());
	Core::App().domain().activeSessionValue(
	) | rpl::map([](Main::Session *session) {
		return session
			? session->data().newItemAdded()
			: rpl::never<not_null<HistoryItem*>>();
	}) | rpl::flatten_latest(
	) | rpl::filter([](not_null<HistoryItem*> item) {
		return item->out() && item->isSending();
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		const auto parent = overlay->parentWidget();
		const auto scale = CurrentScale();
		auto at = QPointF(
			overlay->width() - 60. * scale,
			overlay->height() - 40. * scale);
		const auto focus = QApplication::focusWidget();
		if (parent && focus && parent->isAncestorOf(focus)) {
			const auto rect = QRect(
				focus->mapTo(parent, QPoint()),
				focus->size());
			at = QPointF(
				rect.x() + rect.width() - 30. * scale,
				rect.y() + rect.height() / 2.);
		}
		overlay->burst(at);
		Sounds::Play(Sounds::Sound::Send);
	}, overlay->lifetime());
}

} // namespace Mwtk
