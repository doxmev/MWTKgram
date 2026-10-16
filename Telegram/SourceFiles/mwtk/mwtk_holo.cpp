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

#include <QtCore/QPointer>
#include <QtGui/QConicalGradient>
#include <QtGui/QImage>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>

#include <cmath>

namespace Mwtk::Holo {
namespace {

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

// The whole profile as a hologram: the real widgets stay below (they get
// the mouse and the wheel), but what is seen is their projection. The
// content is captured, turned into a mask of light in the theme color,
// split into color layers and drawn over a dark see-through backdrop
// with scan lines, a sweep, glitches and a shining frame.
class WindowLayer final : public Ui::RpWidget {
public:
	WindowLayer(not_null<QWidget*> section);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void sync();
	void tick();
	void capture();

	const QPointer<QWidget> _section;
	base::Timer _timer;
	crl::time _started = 0;
	crl::time _shown = 0;
	crl::time _lastCapture = 0;
	int _ticks = 0;
	QPixmap _lines;
	QColor _linesColor;
	QImage _holo;
	QImage _ghostA;
	QImage _ghostB;
	QImage _glow;
	QColor _holoColor;

};

WindowLayer::WindowLayer(not_null<QWidget*> section)
: RpWidget(section)
, _section(section.get())
, _timer([=] { tick(); })
, _started(crl::now()) {
	setObjectName(u"mwtk-holo-window"_q);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_OpaquePaintEvent);
	base::install_event_filter(this, section, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Resize:
		case QEvent::ChildAdded:
		case QEvent::Show:
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
		_holo = QImage();
		sync();
	}, lifetime());
	sync();
}

void WindowLayer::sync() {
	const auto enabled = _section
		&& EffectsEnabled()
		&& OptionValue(kOptionHoloProfile);
	if (!enabled) {
		_timer.cancel();
		hide();
		return;
	}
	setGeometry(_section->rect());
	show();
	raise();
	if (!_timer.isActive()) {
		_timer.callEach(std::max(crl::time(CurrentQuality().frameDelay), crl::time(40)));
	}
	capture();
	update();
}

void WindowLayer::tick() {
	if (!_section || isHidden()) {
		return;
	}
	// Fresh content often while the mouse is over the profile (scrolling,
	// hover), rarely when nobody touches it: it is the costly part.
	const auto hovered = _section->underMouse();
	const auto every = hovered ? 1 : 5;
	if (_holo.isNull() || (++_ticks % every) == 0) {
		capture();
	}
	if (window()->isActiveWindow() || hovered) {
		update();
	}
}

void WindowLayer::capture() {
	if (!_section || width() <= 0 || height() <= 0) {
		return;
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
	_lastCapture = crl::now();

	// Light is how far a pixel is from the background: the same for dark
	// and light themes. A lookup table keeps it a single pass over bytes.
	auto gray = shot.convertToFormat(QImage::Format_Grayscale8);
	const auto bg = qGray(base.rgb());
	const auto range = std::max(bg, 255 - bg);
	uchar table[256];
	for (auto i = 0; i != 256; ++i) {
		const auto v = std::abs(i - bg) * 255. / std::max(range, 1) * 1.5;
		table[i] = uchar(std::clamp(int(v + 0.5), 0, 255));
	}
	auto mask = QImage(gray.size(), QImage::Format_Alpha8);
	for (auto y = 0, h = gray.height(); y != h; ++y) {
		const auto from = gray.constScanLine(y);
		const auto to = mask.scanLine(y);
		for (auto x = 0, w = gray.width(); x != w; ++x) {
			to[x] = table[from[x]];
		}
	}
	mask.setDevicePixelRatio(ratio);

	const auto accent = PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;
	_holoColor = accent;
	const auto colored = [&](QColor color) {
		auto result = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		result.fill(color);
		auto q = QPainter(&result);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.drawImage(QPoint(), mask);
		return result;
	};
	_holo = colored(accent.lighter(165));
	_ghostA = colored(QColor(0, 230, 255));
	_ghostB = colored(QColor(255, 60, 200));
	// A cheap blur: down and up again.
	_glow = _holo.scaled(
		std::max(_holo.width() / 6, 1),
		std::max(_holo.height() / 6, 1),
		Qt::IgnoreAspectRatio,
		Qt::FastTransformation).scaled(
			_holo.size(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	_glow.setDevicePixelRatio(ratio);
}

void WindowLayer::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto scale = style::Scale() / 100.;
	const auto active = window()->isActiveWindow();
	if (active || underMouse()) {
		_shown = crl::now() - _started;
	}
	const auto t = _shown / 1000.;
	const auto accent = _holoColor.isValid()
		? _holoColor
		: PaletteAt(
			std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
			0.).first;
	const auto full = QRectF(rect());

	// The projection backdrop: deep and dark, the theme color in it.
	auto deep = QLinearGradient(full.topLeft(), full.bottomLeft());
	deep.setColorAt(0., QColor(6, 8, 16));
	auto mid = accent;
	mid = QColor(mid.red() / 6, mid.green() / 6, mid.blue() / 6 + 8);
	deep.setColorAt(0.5, mid);
	deep.setColorAt(1., QColor(4, 6, 12));
	p.fillRect(full, deep);

	// A faint grid of the projector.
	const auto cell = 24. * scale;
	auto gridColor = accent;
	gridColor.setAlpha(16);
	p.setPen(QPen(gridColor, 1.));
	const auto gridShift = std::fmod(t * 6. * scale, cell);
	for (auto x = std::fmod(full.left(), cell); x < full.right(); x += cell) {
		p.drawLine(QPointF(x, full.top()), QPointF(x, full.bottom()));
	}
	for (auto y = gridShift; y < full.bottom(); y += cell) {
		p.drawLine(QPointF(full.left(), y), QPointF(full.right(), y));
	}

	// The content as light, with a rare flicker.
	const auto flicker = (std::fmod(t, 5.3) < 0.08)
		? 0.55
		: (0.92 + 0.08 * std::sin(t * 7.));
	if (!_holo.isNull()) {
		const auto split = (1.2 + 0.8 * std::sin(t * 1.7)) * scale;
		p.setCompositionMode(QPainter::CompositionMode_Plus);
		p.setOpacity(0.55 * flicker);
		p.drawImage(QPointF(), _glow);
		p.setOpacity(0.35 * flicker);
		p.drawImage(QPointF(-split, 0.), _ghostA);
		p.drawImage(QPointF(split, 0.), _ghostB);
		p.setOpacity(0.9 * flicker);
		p.drawImage(QPointF(), _holo);

		// Now and then a few rows slip sideways for a moment.
		const auto glitch = std::fmod(t, 3.7);
		if (glitch < 0.14) {
			const auto ratio = _holo.devicePixelRatio();
			const auto seed = int(std::floor(t / 3.7));
			for (auto i = 0; i != 3; ++i) {
				const auto k = (seed * 7919 + i * 104729) % 1000;
				const auto y = full.height() * (k / 1000.);
				const auto h = (6 + (k % 18)) * scale;
				const auto dx = ((k % 2) ? 1 : -1) * (4 + k % 9) * scale;
				const auto source = QRectF(0., y, full.width(), h);
				p.setOpacity(0.8);
				p.drawImage(
					source.translated(dx, 0.),
					_holo,
					QRectF(
						source.x() * ratio,
						source.y() * ratio,
						source.width() * ratio,
						source.height() * ratio));
			}
		}
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		p.setOpacity(1.);
	}

	// Scan lines moving down (a tiled pixmap, made once per color).
	const auto step = std::max(int(3 * scale), 2);
	if (_lines.isNull() || _linesColor != accent) {
		_linesColor = accent;
		_lines = QPixmap(QSize(64, step * 16));
		_lines.fill(Qt::transparent);
		auto q = QPainter(&_lines);
		for (auto y = 0; y < _lines.height(); y += step) {
			q.fillRect(0, y, _lines.width(), 1, QColor(0, 0, 0, 70));
		}
	}
	const auto shift = int(std::fmod(t * 18. * scale, double(step * 16)));
	p.drawTiledPixmap(rect(), _lines, QPoint(0, -shift));

	// A light sweep crossing the projection from time to time.
	const auto phase = std::fmod(t / 4.5, 1.);
	const auto y = full.top() - full.height() * 0.4 + phase * full.height() * 1.8;
	auto band = QLinearGradient(QPointF(0., y), QPointF(0., y + full.height() * 0.25));
	auto light = accent.lighter(170);
	light.setAlpha(0);
	band.setColorAt(0., light);
	light.setAlpha(40);
	band.setColorAt(0.5, light);
	light.setAlpha(0);
	band.setColorAt(1., light);
	p.fillRect(full, band);

	// A shining iridescent frame.
	p.setRenderHint(QPainter::Antialiasing);
	auto rim = QConicalGradient(full.center(), std::fmod(t * 30., 360.));
	rim.setColorAt(0., accent);
	rim.setColorAt(0.25, accent.lighter(150));
	rim.setColorAt(0.5, QColor(0, 230, 255));
	rim.setColorAt(0.75, accent.lighter(130));
	rim.setColorAt(1., accent);
	p.setBrush(Qt::NoBrush);
	const auto width = 2. * scale;
	p.setPen(QPen(QBrush(rim), width));
	p.setOpacity(0.85);
	p.drawRect(full.adjusted(width / 2., width / 2., -width / 2., -width / 2.));
}

} // namespace

void AttachWindow(not_null<QWidget*> inside) {
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
	new WindowLayer(not_null<QWidget*>(section)); // owned by the section
}

void Attach(not_null<QWidget*> userpic) {
	const auto parent = userpic->parentWidget();
	if (!parent) {
		return;
	}
	Ui::CreateChild<Layer>(parent, userpic);
}

} // namespace Mwtk::Holo
