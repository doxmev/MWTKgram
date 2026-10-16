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

#include <QtCore/QPointer>
#include <QtGui/QConicalGradient>
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

// The whole profile as a hologram. Cheap to paint: the scan lines are a
// tiled pixmap, the rest are two gradients and a frame.
class WindowLayer final : public Ui::RpWidget {
public:
	WindowLayer(not_null<QWidget*> section);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void sync();

	const QPointer<QWidget> _section;
	base::Timer _timer;
	crl::time _started = 0;
	crl::time _shown = 0;
	QPixmap _lines;
	QColor _linesColor;

};

WindowLayer::WindowLayer(not_null<QWidget*> section)
: RpWidget(section)
, _section(section.get())
, _timer([=] { update(); })
, _started(crl::now()) {
	setObjectName(u"mwtk-holo-window"_q);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
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
		// The layer covers a big area: 25 fps is plenty for a slow sweep.
		_timer.callEach(std::max(crl::time(CurrentQuality().frameDelay), crl::time(40)));
	}
}

void WindowLayer::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto scale = style::Scale() / 100.;
	const auto active = window()->isActiveWindow();
	if (active) {
		_shown = crl::now() - _started;
	}
	const auto t = _shown / 1000.;
	const auto accent = PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;
	const auto full = QRectF(rect());

	// Tint of the theme color, slowly breathing, with a rare flicker.
	const auto flicker = (std::fmod(t, 5.3) < 0.08) ? 0.5 : 1.;
	auto tint = accent;
	tint.setAlphaF(float((0.07 + 0.03 * std::sin(t * 1.3)) * flicker));
	p.fillRect(full, tint);

	// Scan lines moving down (a tiled pixmap, made once per color).
	const auto step = std::max(int(4 * scale), 3);
	if (_lines.isNull() || _linesColor != accent) {
		_linesColor = accent;
		_lines = QPixmap(QSize(64, step * 16));
		_lines.fill(Qt::transparent);
		auto q = QPainter(&_lines);
		auto line = accent.lighter(160);
		line.setAlpha(26);
		for (auto y = 0; y < _lines.height(); y += step) {
			q.fillRect(0, y, _lines.width(), 1, line);
		}
	}
	const auto shift = int(std::fmod(t * 18. * scale, double(step * 16)));
	p.drawTiledPixmap(rect(), _lines, QPoint(0, -shift));

	// A light sweep crossing the window from time to time.
	const auto phase = std::fmod(t / 4.5, 1.);
	const auto y = full.top() - full.height() * 0.4 + phase * full.height() * 1.8;
	auto band = QLinearGradient(QPointF(0., y), QPointF(0., y + full.height() * 0.25));
	auto light = accent.lighter(170);
	light.setAlpha(0);
	band.setColorAt(0., light);
	light.setAlpha(34);
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
