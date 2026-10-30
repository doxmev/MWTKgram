/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_holo.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "info/info_wrap_widget.h"
#include "mwtk/mwtk_options.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "styles/style_basic.h"

#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>

#include <QtCore/QPointer>
#include <QtGui/QConicalGradient>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Mwtk::Holo {
namespace {

bool GlobalCapturing = false;

class Layer final : public Ui::RpWidget {
public:
	Layer(QWidget *parent, not_null<QWidget*> target);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void sync();

	const QPointer<QWidget> _target;
	base::Timer _timer;
	crl::time _started = 0;
	crl::time _shown = 0;

};

Layer::Layer(QWidget *parent, not_null<QWidget*> target)
: RpWidget(parent)
, _target(target.get())
, _timer([=] { update(); })
, _started(crl::now()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	base::install_event_filter(this, target, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Move:
		case QEvent::Resize:
		case QEvent::Show:
		case QEvent::Hide:
			crl::on_main(this, [=] { sync(); });
			break;
		default:
			break;
		}
		return base::EventFilterResult::Continue;
	});
	rpl::merge(
		OptionChanges(kOptionHoloProfile),
		OptionChanges(kOptionPalette),
		ConfigChanges()
	) | rpl::on_next([=] {
		sync();
	}, lifetime());
	sync();
}

void Layer::sync() {
	const auto enabled = _target
		&& !_target->isHidden()
		&& EffectsEnabled()
		&& OptionValue(kOptionHoloProfile);
	if (!enabled) {
		_timer.cancel();
		hide();
		return;
	}
	const auto margin = int(8 * style::Scale() / 100.);
	setGeometry(_target->geometry().marginsAdded({ margin, margin, margin, margin }));
	show();
	raise();
	if (!_timer.isActive()) {
		_timer.callEach(std::max(crl::time(CurrentQuality().frameDelay), crl::time(33)));
	}
}

void Layer::paintEvent(QPaintEvent *e) {
	if (!_target) {
		return;
	}
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	const auto scale = style::Scale() / 100.;
	const auto margin = 8. * scale;
	const auto inner = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
	const auto center = inner.center();
	const auto radius = std::min(inner.width(), inner.height()) / 2.;
	// Frozen while the window is in the background.
	const auto active = window()->isActiveWindow();
	if (active) {
		_shown = crl::now() - _started;
	}
	const auto t = _shown / 1000.;

	// Iridescent rim.
	auto rim = QConicalGradient(center, std::fmod(t * 40., 360.));
	const auto accent = PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;
	rim.setColorAt(0., accent);
	rim.setColorAt(0.25, QColor(170, 90, 255));
	rim.setColorAt(0.5, QColor(0, 230, 255));
	rim.setColorAt(0.75, QColor(120, 255, 170));
	rim.setColorAt(1., accent);
	p.setBrush(Qt::NoBrush);
	for (auto i = 3; i != 0; --i) {
		auto pen = QPen(QBrush(rim), (1.2 + i * 1.6) * scale);
		p.setOpacity(0.14 * (4 - i));
		p.setPen(pen);
		p.drawEllipse(center, radius + 1.5 * scale, radius + 1.5 * scale);
	}
	p.setOpacity(0.9);
	p.setPen(QPen(QBrush(rim), 1.6 * scale));
	p.drawEllipse(center, radius + 1.5 * scale, radius + 1.5 * scale);

	// Scan lines and a light sweep inside the userpic.
	auto circle = QPainterPath();
	circle.addEllipse(center, radius, radius);
	p.setClipPath(circle);
	p.setOpacity(1.);
	p.setPen(QPen(QColor(255, 255, 255, 14), 1.));
	const auto shift = std::fmod(t * 12. * scale, 4. * scale);
	for (auto y = inner.top() + shift; y < inner.bottom(); y += 4. * scale) {
		p.drawLine(QPointF(inner.left(), y), QPointF(inner.right(), y));
	}
	const auto phase = std::fmod(t / 3.2, 1.);
	const auto x = inner.left() - inner.width() * 0.5 + phase * inner.width() * 2.;
	auto band = QLinearGradient(QPointF(x, inner.top()), QPointF(x + inner.width() * 0.4, inner.bottom()));
	band.setColorAt(0., QColor(255, 255, 255, 0));
	band.setColorAt(0.5, QColor(160, 230, 255, 46));
	band.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillRect(inner, band);
}

// The whole profile as a glass hologram card. The real widgets stay
// below (they get clicks and the wheel), what is seen is their capture:
// the background keyed out to thin glass (the grey head stays dense), the
// parts floating at different depths, and what is behind the profile seen
// through. Holding the right mouse button turns the card in 3D; on release
// it comes back. A short right click still opens the usual menu.
class WindowLayer final : public Ui::RpWidget {
public:
	WindowLayer(
		not_null<QWidget*> section,
		not_null<QWidget*> head,
		QWidget *userpic);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Part {
		QImage image;
		QRectF rect; // in the section, logical pixels
		double depth = 0.;
	};

	void sync();
	void tick();
	void capture();
	void captureBehind();
	bool filterApp(not_null<QEvent*> e);
	[[nodiscard]] QPointF project(QPointF point, double depth) const;
	[[nodiscard]] QPolygonF quad(QRectF rect, double depth) const;
	void paintCard(QPainter &p);

	const QPointer<QWidget> _section;
	const QPointer<QWidget> _head;
	const QPointer<QWidget> _userpic;
	base::Timer _timer;
	crl::time _started = 0;
	int _ticks = 0;
	QImage _behind;
	QImage _content;
	std::vector<Part> _parts;
	QImage _cached;
	bool _cacheValid = false;
	QPixmap _lines;
	QColor _linesColor;

	// Rotation in degrees; the card rests slightly turned.
	static constexpr auto kPlane = 8.;
	static constexpr auto kRestX = -3.;
	static constexpr auto kRestY = 5.;
	double _rx = kRestX;
	double _ry = kRestY;
	double _cachedRx = 1000.;
	double _cachedRy = 1000.;
	bool _pressed = false;
	bool _dragging = false;
	crl::time _eatMenuUntil = 0;
	QPoint _pressAt;
	double _pressRx = 0.;
	double _pressRy = 0.;

};

WindowLayer::WindowLayer(
	not_null<QWidget*> section,
	not_null<QWidget*> head,
	QWidget *userpic)
: RpWidget(section)
, _section(section.get())
, _head(head.get())
, _userpic(userpic)
, _timer([=] { tick(); })
, _started(crl::now()) {
	setObjectName(u"mwtk-holo-window"_q);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_OpaquePaintEvent);
	base::install_event_filter(this, section, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Resize:
			_behind = QImage();
			[[fallthrough]];
		case QEvent::ChildAdded:
		case QEvent::Show:
			crl::on_main(this, [=] { sync(); });
			break;
		default:
			break;
		}
		return base::EventFilterResult::Continue;
	});
	base::install_event_filter(this, qApp, [=](not_null<QEvent*> e) {
		return filterApp(e)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	rpl::merge(
		OptionChanges(kOptionHoloProfile),
		OptionChanges(kOptionPalette),
		ConfigChanges()
	) | rpl::on_next([=] {
		_cacheValid = false;
		sync();
	}, lifetime());
	sync();
}

void WindowLayer::sync() {
	const auto enabled = _section
		&& _head
		&& EffectsEnabled()
		&& OptionValue(kOptionHoloProfile);
	if (!enabled) {
		_timer.cancel();
		hide();
		return;
	}
	setGeometry(_section->rect());
	if (isHidden()) {
		_behind = QImage();
	}
	show();
	raise();
	if (!_timer.isActive()) {
		_timer.callEach(std::max(crl::time(CurrentQuality().frameDelay), crl::time(40)));
	}
	capture();
	update();
}

bool WindowLayer::filterApp(not_null<QEvent*> e) {
	const auto type = e->type();
	if (type != QEvent::MouseButtonPress
		&& type != QEvent::MouseMove
		&& type != QEvent::MouseButtonRelease
		&& type != QEvent::ContextMenu) {
		return false;
	}
	if (isHidden() || !_section) {
		return false;
	}
	if (type == QEvent::ContextMenu) {
		const auto eat = (crl::now() < _eatMenuUntil);
		_eatMenuUntil = 0;
		return eat;
	}
	const auto mouse = static_cast<QMouseEvent*>(e.get());
	const auto global = mouse->globalPosition().toPoint();
	if (type == QEvent::MouseButtonPress) {
		if (mouse->button() != Qt::RightButton) {
			return false;
		}
		const auto under = QApplication::widgetAt(global);
		if (!under || !_section->isAncestorOf(under)) {
			return false;
		}
		_pressed = true;
		_dragging = false;
		_pressAt = global;
		_pressRx = _rx;
		_pressRy = _ry;
		return false; // a short right click still works as usual
	}
	if (!_pressed) {
		return false;
	}
	if (type == QEvent::MouseMove) {
		if (!(mouse->buttons() & Qt::RightButton)) {
			_pressed = _dragging = false;
			return false;
		}
		const auto delta = global - _pressAt;
		if (!_dragging && delta.manhattanLength() < 6) {
			return false;
		}
		_dragging = true;
		_ry = std::clamp(_pressRy + delta.x() * 0.35, -60., 60.);
		_rx = std::clamp(_pressRx - delta.y() * 0.35, -45., 45.);
		update();
		return true;
	}
	// Release.
	if (mouse->button() != Qt::RightButton) {
		return false;
	}
	const auto dragged = _dragging;
	_pressed = _dragging = false;
	if (dragged) {
		// The menu comes right after the release: not this time.
		_eatMenuUntil = crl::now() + 500;
	}
	return dragged;
}

void WindowLayer::tick() {
	if (!_section || isHidden()) {
		return;
	}
	if (!_dragging
		&& (std::abs(_rx - kRestX) > 0.05 || std::abs(_ry - kRestY) > 0.05)) {
		// Spring back.
		_rx += (kRestX - _rx) * 0.18;
		_ry += (kRestY - _ry) * 0.18;
	}
	// Fresh content often while the mouse is over the profile (scrolling,
	// hover), rarely when nobody touches it: it is the costly part.
	const auto hovered = _section->underMouse() || _dragging;
	if (_content.isNull() || (++_ticks % (hovered ? 1 : 5)) == 0) {
		capture();
	}
	if (window()->isActiveWindow() || hovered) {
		update();
	}
}

// What is behind the profile: everything below it, level by level.
void WindowLayer::captureBehind() {
	const auto ratio = style::DevicePixelRatio();
	_behind = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	_behind.setDevicePixelRatio(ratio);
	_behind.fill(QColor(8, 9, 14));
	const auto top = _section->window();
	auto levels = std::vector<std::vector<QWidget*>>();
	auto child = _section.data();
	while (child && child != top) {
		const auto parent = child->parentWidget();
		if (!parent) {
			break;
		}
		auto lower = std::vector<QWidget*>();
		for (const auto object : parent->children()) {
			if (object == child) {
				break;
			}
			const auto widget = qobject_cast<QWidget*>(object);
			if (widget && widget->isVisible() && !widget->isWindow()) {
				lower.push_back(widget);
			}
		}
		levels.push_back(std::move(lower));
		child = parent;
	}
	const auto origin = _section->mapTo(top, QPoint());
	auto q = QPainter(&_behind);
	// The chats skip painting while a layer covers them: not now.
	GlobalCapturing = true;
	const auto guard = gsl::finally([] { GlobalCapturing = false; });
	for (auto i = levels.rbegin(); i != levels.rend(); ++i) {
		for (const auto widget : *i) {
			const auto at = widget->mapTo(top, QPoint()) - origin;
			if (!QRect(at, widget->size()).intersects(rect())) {
				continue;
			}
			widget->render(
				&q,
				at,
				QRegion(),
				QWidget::DrawWindowBackground | QWidget::DrawChildren);
		}
	}
	q.end();
	// Softly out of focus, like seen through glass.
	const auto small = _behind.scaled(
		std::max(_behind.width() / 2, 1),
		std::max(_behind.height() / 2, 1),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	_behind = small.scaled(
		_behind.size(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	_behind.setDevicePixelRatio(ratio);
}

void WindowLayer::capture() {
	if (!_section || !_head || width() <= 0 || height() <= 0) {
		return;
	}
	if (_behind.isNull()) {
		captureBehind();
	}
	const auto ratio = style::DevicePixelRatio();
	auto shot = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	shot.setDevicePixelRatio(ratio);
	const auto base = st::windowBg->c;
	shot.fill(base);
	{
		auto q = QPainter(&shot);
		for (const auto child : _section->children()) {
			const auto widget = qobject_cast<QWidget*>(child);
			if (!widget || widget == this || widget->isHidden()) {
				continue;
			}
			widget->render(
				&q,
				widget->pos(),
				QRegion(),
				QWidget::DrawWindowBackground | QWidget::DrawChildren);
		}
	}

	// The background becomes thin glass: a pixel is as solid as it is
	// different from the background color. The grey head stays dense.
	const auto head = QRect(
		_head->mapTo(_section, QPoint()),
		_head->size()).intersected(rect());
	const auto headTop = head.top() * ratio;
	const auto headBottom = (head.y() + head.height()) * ratio;
	const auto br = base.red(), bgc = base.green(), bb = base.blue();
	for (auto y = 0, h = shot.height(); y != h; ++y) {
		const auto line = reinterpret_cast<QRgb*>(shot.scanLine(y));
		const auto minimum = (y >= headTop && y < headBottom) ? 160 : 60;
		for (auto x = 0, w = shot.width(); x != w; ++x) {
			const auto c = line[x];
			const auto diff = std::max({
				std::abs(qRed(c) - br),
				std::abs(qGreen(c) - bgc),
				std::abs(qBlue(c) - bb) });
			const auto a = std::clamp(std::max(diff * 4 + 10, minimum), 0, 255);
			line[x] = qRgba(
				qRed(c) * a / 255,
				qGreen(c) * a / 255,
				qBlue(c) * a / 255,
				a);
		}
	}
	_content = std::move(shot);

	// The parts and their depths.
	_parts.clear();
	const auto cut = [&](QRect r) {
		r = r.intersected(rect());
		auto result = _content.copy(QRect(r.topLeft() * ratio, r.size() * ratio));
		result.setDevicePixelRatio(ratio);
		return result;
	};
	// One glass plane with the userpic floating a little above it.
	const auto ring = int(8 * style::Scale() / 100.);
	const auto avatar = _userpic
		? QRect(_userpic->mapTo(_section, QPoint()), _userpic->size())
			.marginsAdded({ ring, ring, ring, ring })
			.intersected(head)
		: QRect();
	auto plane = cut(rect());
	auto avatarImage = QImage();
	if (!avatar.isEmpty()) {
		avatarImage = QImage(
			avatar.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		avatarImage.setDevicePixelRatio(ratio);
		avatarImage.fill(Qt::transparent);
		auto circle = QPainterPath();
		circle.addEllipse(QRectF(QPointF(), QSizeF(avatar.size())));
		{
			auto q = QPainter(&avatarImage);
			q.setRenderHint(QPainter::Antialiasing);
			q.setClipPath(circle);
			q.drawImage(QPointF(), cut(avatar));
		}
		// Where the userpic was, the head color: no hole in the card.
		const auto sampleAt = QPoint(
			std::max(avatar.x() - ring * 2, head.x() + 1),
			avatar.y() + avatar.height() / 2) * ratio;
		const auto fillColor = QColor::fromRgba(
			plane.pixel(sampleAt.x(), sampleAt.y()));
		auto q = QPainter(&plane);
		q.setRenderHint(QPainter::Antialiasing);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setPen(Qt::NoPen);
		q.setBrush(fillColor);
		q.drawEllipse(QRectF(avatar).adjusted(-1, -1, 1, 1));
	}
	_parts.push_back({ std::move(plane), QRectF(rect()), kPlane });
	if (!avatarImage.isNull()) {
		_parts.push_back({ std::move(avatarImage), QRectF(avatar), kPlane + 28. });
	}
	_cacheValid = false;
}

QPointF WindowLayer::project(QPointF point, double depth) const {
	const auto scale = style::Scale() / 100.;
	const auto cx = width() / 2., cy = height() / 2.;
	const auto shrink = 0.9;
	auto x = (point.x() - cx) * shrink;
	auto y = (point.y() - cy) * shrink;
	auto z = depth * scale;
	const auto ay = _ry * std::numbers::pi / 180.;
	const auto ax = _rx * std::numbers::pi / 180.;
	// Around Y, then around X.
	const auto x1 = x * std::cos(ay) + z * std::sin(ay);
	const auto z1 = -x * std::sin(ay) + z * std::cos(ay);
	const auto y2 = y * std::cos(ax) - z1 * std::sin(ax);
	const auto z2 = y * std::sin(ax) + z1 * std::cos(ax);
	const auto distance = 1400. * scale;
	const auto f = distance / std::max(distance - z2, 1.);
	return { cx + x1 * f, cy + y2 * f };
}

QPolygonF Corners(QRectF rect) {
	return QPolygonF({
		rect.topLeft(),
		rect.topRight(),
		rect.bottomRight(),
		rect.bottomLeft(),
	});
}

QPolygonF WindowLayer::quad(QRectF rect, double depth) const {
	return QPolygonF({
		project(rect.topLeft(), depth),
		project(rect.topRight(), depth),
		project(rect.bottomRight(), depth),
		project(rect.bottomLeft(), depth),
	});
}

void WindowLayer::paintCard(QPainter &p) {
	const auto accent = PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);

	// The glass body: a faint tint of the theme color.
	const auto body = quad(QRectF(rect()), kPlane);
	auto glass = accent;
	glass.setAlpha(26);
	p.setPen(Qt::NoPen);
	p.setBrush(glass);
	p.drawPolygon(body);

	for (const auto &part : _parts) {
		const auto target = quad(part.rect, part.depth);
		auto transform = QTransform();
		const auto source = Corners(QRectF(QPointF(), part.rect.size()));
		if (!QTransform::quadToQuad(source, target, transform)) {
			continue;
		}
		p.save();
		p.setTransform(transform, true);
		p.drawImage(QPointF(), part.image);
		p.restore();
	}
}

void WindowLayer::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto scale = style::Scale() / 100.;
	const auto t = (crl::now() - _started) / 1000.;
	const auto accent = PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;
	const auto full = QRectF(rect());

	if (_behind.isNull()) {
		p.fillRect(rect(), QColor(8, 9, 14));
	} else {
		p.drawImage(QPointF(), _behind);
	}

	// The card itself is cached while it rests: only the light moves.
	const auto ratio = style::DevicePixelRatio();
	if (!_cacheValid || _cachedRx != _rx || _cachedRy != _ry
		|| _cached.size() != size() * ratio) {
		_cached = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
		_cached.setDevicePixelRatio(ratio);
		_cached.fill(Qt::transparent);
		auto q = QPainter(&_cached);
		paintCard(q);
		_cacheValid = true;
		_cachedRx = _rx;
		_cachedRy = _ry;
	}
	const auto flicker = (std::fmod(t, 6.7) < 0.07) ? 0.6 : 1.;
	p.setOpacity(flicker);
	p.drawImage(QPointF(), _cached);
	p.setOpacity(1.);

	const auto body = quad(full, kPlane);
	auto path = QPainterPath();
	path.addPolygon(body);
	path.closeSubpath();
	p.save();
	p.setClipPath(path);

	// Now and then a thin slice slips sideways.
	const auto glitch = std::fmod(t, 3.1);
	if (glitch < 0.11 && !_content.isNull()) {
		const auto seed = int(std::floor(t / 3.1));
		const auto y = full.height() * (((seed * 7919) % 1000) / 1000.);
		const auto h = (6 + (seed % 10)) * scale;
		const auto dx = ((seed % 2) ? 1 : -1) * (6 + seed % 8) * scale;
		const auto slice = QRectF(0., y, full.width(), h);
		auto transform = QTransform();
		if (QTransform::quadToQuad(
				Corners(slice),
				quad(slice.translated(dx, 0.), kPlane),
				transform)) {
			p.save();
			p.setTransform(transform, true);
			p.setOpacity(0.85);
			p.drawImage(
				slice,
				_content,
				QRectF(
					slice.x() * ratio,
					slice.y() * ratio,
					slice.width() * ratio,
					slice.height() * ratio));
			p.restore();
		}
	}

	// Scan lines moving down (a tiled pixmap, made once per color).
	const auto step = std::max(int(3 * scale), 2);
	if (_lines.isNull() || _linesColor != accent) {
		_linesColor = accent;
		_lines = QPixmap(QSize(64, step * 16));
		_lines.fill(Qt::transparent);
		auto q = QPainter(&_lines);
		auto line = accent.lighter(170);
		line.setAlpha(18);
		for (auto y = 0; y < _lines.height(); y += step) {
			q.fillRect(0, y, _lines.width(), 1, line);
		}
	}
	const auto shift = int(std::fmod(t * 18. * scale, double(step * 16)));
	p.drawTiledPixmap(rect(), _lines, QPoint(0, -shift));

	// A light sheen that follows the turn of the card.
	const auto along = 0.5 - _ry / 90.;
	const auto sx = full.width() * along;
	auto sheen = QLinearGradient(
		QPointF(sx - full.width() * 0.5, 0.),
		QPointF(sx + full.width() * 0.5, full.height() * 0.6));
	sheen.setColorAt(0., QColor(255, 255, 255, 0));
	sheen.setColorAt(0.45, QColor(255, 255, 255, 28));
	sheen.setColorAt(0.5, QColor(120, 230, 255, 30));
	auto pinkish = accent;
	pinkish.setAlpha(26);
	sheen.setColorAt(0.55, pinkish);
	sheen.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillRect(full, sheen);
	p.restore();

	// A shining iridescent frame around the glass.
	p.setRenderHint(QPainter::Antialiasing);
	auto rim = QConicalGradient(full.center(), std::fmod(t * 60., 360.));
	rim.setColorAt(0., accent);
	rim.setColorAt(0.33, QColor(80, 225, 255));
	rim.setColorAt(0.66, QColor(176, 107, 255));
	rim.setColorAt(1., accent);
	p.setBrush(Qt::NoBrush);
	for (auto i = 3; i != 0; --i) {
		p.setOpacity(0.12 * (4 - i));
		p.setPen(QPen(QBrush(rim), (1.5 + i * 2.5) * scale));
		p.drawPolygon(body);
	}
	p.setOpacity(0.95);
	p.setPen(QPen(QBrush(rim), 2. * scale));
	p.drawPolygon(body);
}

} // namespace

bool Capturing() {
	return GlobalCapturing;
}

void AttachWindow(not_null<QWidget*> inside, QWidget *userpic) {
	auto section = inside->parentWidget();
	while (section && !dynamic_cast<Info::WrapWidget*>(section)) {
		section = section->parentWidget();
	}
	if (!section
		|| section->findChild<QWidget*>(
			u"mwtk-holo-window"_q,
			Qt::FindDirectChildrenOnly)) {
		return;
	}
	new WindowLayer(not_null<QWidget*>(section), inside, userpic); // owned by the section
}

void Attach(not_null<QWidget*> userpic) {
	const auto parent = userpic->parentWidget();
	if (!parent) {
		return;
	}
	Ui::CreateChild<Layer>(parent, userpic);
}

} // namespace Mwtk::Holo
