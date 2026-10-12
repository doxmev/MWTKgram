/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_holo.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "mwtk/mwtk_options.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"

#include <QtCore/QPointer>
#include <QtGui/QConicalGradient>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

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

} // namespace

void Attach(not_null<QWidget*> userpic) {
	const auto parent = userpic->parentWidget();
	if (!parent) {
		return;
	}
	Ui::CreateChild<Layer>(parent, userpic);
}

} // namespace Mwtk::Holo
