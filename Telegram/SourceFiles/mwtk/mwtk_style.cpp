/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_style.h"

#include "mwtk/mwtk_options.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "ui/chat/message_bubble.h"
#include "ui/style/style_core_scale.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "base/call_delayed.h"
#include "base/flat_map.h"
#include "styles/style_dialogs.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtCore/QPointer>

namespace Mwtk {
namespace {

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double k) {
	const auto r = std::clamp(k, 0., 1.);
	return QColor::fromRgbF(
		float(a.redF() * (1. - r) + b.redF() * r),
		float(a.greenF() * (1. - r) + b.greenF() * r),
		float(a.blueF() * (1. - r) + b.blueF() * r),
		float(a.alphaF() * (1. - r) + b.alphaF() * r));
}

void ApplyChatStyle() {
	const auto effects = EffectsEnabled();
	const auto volumetric = effects && OptionValue(kOptionBubbles3d);
	const auto tinted = effects && OptionValue(kOptionBubblesTint);
	const auto opacityIndex = std::clamp(
		OptionInt(kOptionBubblesOpacity),
		0,
		kBubbleOpacityCount - 1);
	const auto opacity = (opacityIndex == 0)
		? 1.
		: (opacityIndex == 2)
		? 0.55
		: 0.78;
	auto value = Ui::BubbleStyleOverride();
	value.enabled = volumetric || tinted || (effects && opacityIndex != 0);
	value.volumetric = volumetric;
	if (tinted) {
		const auto palette = PaletteAt(OptionInt(kOptionPalette), 0.);
		value.outTint = palette.first;
		value.inTint = palette.third;
		value.tintStrength = 0.42;
	}
	value.opacity = effects ? opacity : 1.;
	Ui::SetBubbleStyleOverride(value);
	for (const auto widget : QApplication::topLevelWidgets()) {
		if (widget->isVisible()) {
			Ui::ForceFullRepaint(widget);
		}
	}
}

[[nodiscard]] bool UserOnline(PeerData *peer) {
	if (!peer) {
		return false;
	}
	if (const auto user = peer->asUser()) {
		return !user->isSelf() && !user->isBot() && Data::IsUserOnline(user);
	}
	return false;
}

[[nodiscard]] QColor OnlineColor() {
	return CurrentAvatarGlow().online;
}

[[nodiscard]] base::flat_map<QPaintDevice*, QRegion> &PulseTargets() {
	static auto result = base::flat_map<QPaintDevice*, QRegion>();
	return result;
}

} // namespace

void SetupChatStyle() {
	static auto done = false;
	if (done) {
		return;
	}
	done = true;
	static auto lifetime = rpl::lifetime();
	ChatStyleChanges(
	) | rpl::on_next([] {
		ApplyChatStyle();
	}, lifetime);
	ApplyChatStyle();
}

bool PaintDialogCard(
		QPainter &p,
		QRect geometry,
		const QColor &background,
		bool active,
		bool selected,
		PeerData *peer) {
	const auto style = CurrentCardStyle();
	if (!style.enabled) {
		return false;
	}
	p.fillRect(geometry, background);
	const auto scale = style::Scale() / 100.;
	const auto alpha = std::clamp(style.alpha, 0.1, 1.);
	const auto inset = QRectF(geometry).marginsRemoved({
		6. * scale,
		2. * scale,
		6. * scale,
		4. * scale,
	});
	if (inset.width() <= 0 || inset.height() <= 0) {
		return true;
	}
	const auto radius = std::min(10. * scale, inset.height() / 3.);
	auto base = active
		? Mix(st::dialogsBgActive->c, style.accent, 0.4)
		: selected
		? Mix(st::dialogsBgOver->c, style.accent, 0.16)
		: Mix(background, style.accent, 0.07).lighter(108);
	auto path = QPainterPath();
	path.addRoundedRect(inset, radius, radius);

	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setOpacity(p.opacity() * (active ? std::max(alpha, 0.6) : alpha));
	const auto depth = std::max(1., 2.5 * scale);
	p.setBrush(QColor(0, 0, 0, 36));
	p.drawPath(path.translated(0., depth + 1.5));
	p.setBrush(base.darker(150));
	p.drawPath(path.translated(0., depth));
	auto face = QLinearGradient(inset.topLeft(), inset.bottomLeft());
	face.setColorAt(0., base.lighter(112));
	face.setColorAt(1., base);
	p.setBrush(face);
	p.drawPath(path);
	p.setClipPath(path);
	auto gloss = QLinearGradient(
		inset.topLeft(),
		QPointF(inset.left(), inset.top() + inset.height() * 0.45));
	gloss.setColorAt(0., QColor(255, 255, 255, active ? 40 : 26));
	gloss.setColorAt(1., QColor(255, 255, 255, 0));
	p.fillRect(inset, gloss);
	p.setBrush(Qt::NoBrush);
	const auto online = UserOnline(peer);
	const auto glowCard = online && OptionValue(kOptionOnlineCardGlow);
	p.setPen(QPen(
		glowCard
			? [] { auto c = OnlineColor(); c.setAlpha(150); return c; }()
			: active
			? QColor(255, 255, 255, 70)
			: QColor(255, 255, 255, 22),
		glowCard ? 1.6 : 1.));
	p.drawPath(path);
	if (glowCard) {
		auto inner = QRadialGradient(
			QPointF(inset.left() + inset.height() * 0.6, inset.center().y()),
			inset.width() * 0.6);
		auto c0 = OnlineColor();
		c0.setAlpha(46);
		auto c1 = c0;
		c1.setAlpha(0);
		inner.setColorAt(0., c0);
		inner.setColorAt(1., c1);
		p.fillRect(inset, inner);
	}
	p.setClipping(false);
	if (OptionValue(kOptionOnlineStripe)) {
		auto color = online ? OnlineColor() : style.accent.darker(130);
		color.setAlpha(online ? 255 : 170);
		const auto stripe = QRectF(
			inset.left() + 1.,
			inset.top() + inset.height() * 0.22,
			3. * scale,
			inset.height() * 0.56);
		if (online) {
			auto halo = color;
			halo.setAlpha(70);
			p.setPen(Qt::NoPen);
			p.setBrush(halo);
			p.drawRoundedRect(stripe.adjusted(-2. * scale, -2. * scale, 2. * scale, 2. * scale), 3. * scale, 3. * scale);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(stripe, 1.5 * scale, 1.5 * scale);
	}
	p.restore();
	return true;
}

bool OnlineDotShown() {
	if (!EffectsEnabled()) {
		return true;
	}
	return OptionValue(kOptionOnlineDot) || OptionValue(kOptionOnlinePulse);
}

void PaintOnlineExtras(
		QPainter &p,
		QRect userpic,
		PeerData *peer,
		Ui::PeerUserpicView &view,
		const QColor &background) {
	if (!EffectsEnabled() || !peer || userpic.isEmpty() || !UserOnline(peer)) {
		return;
	}
	const auto tile = OptionValue(kOptionOnlineTile);
	const auto pulse = OptionValue(kOptionOnlinePulse);
	const auto dot = OptionValue(kOptionOnlineDot) || pulse;
	if (!tile && !pulse) {
		return;
	}
	const auto scale = style::Scale() / 100.;
	const auto size = userpic.width();
	const auto dotRadius = 5.5 * scale;
	const auto center = QPointF(
		userpic.x() + size - dotRadius - 1. * scale,
		userpic.y() + size - dotRadius - 1. * scale);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	if (tile) {
		// Cover the round userpic and its badge with a rounded tile.
		peer->paintUserpic(p, view, {
			.position = userpic.topLeft(),
			.size = size,
			.shape = Ui::PeerUserpicShape::Forum,
		});
		if (dot) {
			p.setPen(Qt::NoPen);
			p.setBrush(background);
			p.drawEllipse(center, dotRadius + 2. * scale, dotRadius + 2. * scale);
			p.setBrush(OnlineColor());
			p.drawEllipse(center, dotRadius, dotRadius);
		}
	}
	if (pulse) {
		const auto t = (crl::now() % 1800) / 1800.;
		auto color = OnlineColor();
		color.setAlphaF(float(0.7 * (1. - t)));
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, 2. * scale));
		const auto r = dotRadius + 8. * scale * t;
		p.drawEllipse(center, r, r);
		const auto reach = int(std::ceil(dotRadius + 10. * scale));
		const auto local = QRect(
			int(center.x()) - reach,
			int(center.y()) - reach,
			reach * 2,
			reach * 2);
		PulseTargets()[p.device()] += p.transform().mapRect(local);
	}
	p.restore();
}

void SchedulePulseRepaint(not_null<QWidget*> widget) {
	auto &targets = PulseTargets();
	const auto i = targets.find(widget.get());
	if (i == targets.end()) {
		return;
	}
	const auto region = i->second;
	targets.erase(i);
	if (region.isEmpty()) {
		return;
	}
	const auto weak = QPointer<QWidget>(widget.get());
	base::call_delayed(66, widget.get(), [=] {
		if (weak) {
			weak->update(region);
		}
	});
}

void PaintAvatarRing(QPainter &p, QRect rect, PeerData *peer) {
	if (!peer || rect.isEmpty()) {
		return;
	}
	const auto glow = CurrentAvatarGlow();
	if (!glow.enabled) {
		return;
	}
	auto online = false;
	if (const auto user = peer->asUser()) {
		online = !user->isSelf()
			&& !user->isBot()
			&& Data::IsUserOnline(user);
	}
	const auto color = online ? glow.online : glow.offline;
	// A soft halo outside the userpic, not a crisp ring: a ring reads as
	// an unseen story.
	const auto center = QRectF(rect).center();
	const auto r = rect.width() / 2.;
	const auto outer = r * 1.32;
	auto halo = QRadialGradient(center, outer);
	const auto edge = std::clamp(r / outer, 0.1, 0.99);
	auto c0 = color;
	c0.setAlphaF(online ? 0.55f : 0.32f);
	auto c1 = color;
	c1.setAlphaF(0.f);
	halo.setColorAt(0., c1);
	halo.setColorAt(edge - 0.02, c1);
	halo.setColorAt(edge, c0);
	halo.setColorAt(1., c1);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(halo);
	p.drawEllipse(center, outer, outer);
	p.restore();
}

} // namespace Mwtk
