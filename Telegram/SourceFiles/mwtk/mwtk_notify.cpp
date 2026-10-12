/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_notify.h"

#include "mwtk/mwtk_options.h"
#include "ui/style/style_core_scale.h"
#include "styles/style_window.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Mwtk::Notify {
namespace {

[[nodiscard]] double Scale() {
	return style::Scale() / 100.;
}

[[nodiscard]] QColor Accent() {
	return PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;
}

[[nodiscard]] QColor WithAlpha(QColor color, int alpha) {
	color.setAlpha(alpha);
	return color;
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double k) {
	return QColor::fromRgbF(
		float(a.redF() * (1. - k) + b.redF() * k),
		float(a.greenF() * (1. - k) + b.greenF() * k),
		float(a.blueF() * (1. - k) + b.blueF() * k),
		float(a.alphaF() * (1. - k) + b.alphaF() * k));
}

} // namespace

QString StyleName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Как в Telegram");
	case 2: return QString::fromUtf8("Неоновая карточка");
	case 3: return QString::fromUtf8("3D-блок");
	}
	return QString::fromUtf8("Голограмма");
}

Style Current() {
	if (!EffectsEnabled()) {
		return Style::Plain;
	}
	const auto value = std::clamp(OptionInt(kOptionNotifyStyle), 0, kStyleCount - 1);
	return Style(value);
}

QRectF CardRect(QSize size) {
	const auto inset = 3. * Scale();
	return QRectF(QPointF(), QSizeF(size)).adjusted(inset, inset, -inset, -inset);
}

void PaintBackground(QPainter &p, QSize size, QRect clip) {
	const auto style = Current();
	const auto rect = CardRect(size);
	const auto scale = Scale();
	const auto radius = ((style == Style::Block) ? 14. : 18.) * scale;
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	const auto base = st::notificationBg->c;
	const auto accent = Accent();

	p.save();
	if (!clip.isEmpty()) {
		p.setClipRect(clip);
	}
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(clip.isEmpty() ? QRect(QPoint(), size) : clip, Qt::transparent);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);

	switch (style) {
	case Style::Hologram: {
		p.setBrush(WithAlpha(base, 236));
		p.drawPath(path);
		auto tint = QLinearGradient(rect.topLeft(), rect.bottomRight());
		tint.setColorAt(0., WithAlpha(accent, 54));
		tint.setColorAt(0.5, QColor(130, 60, 255, 38));
		tint.setColorAt(1., QColor(0, 220, 255, 40));
		p.setBrush(tint);
		p.drawPath(path);
		p.setClipPath(path, clip.isEmpty() ? Qt::ReplaceClip : Qt::IntersectClip);
		p.setPen(QPen(QColor(255, 255, 255, 9), 1.));
		for (auto y = rect.top(); y < rect.bottom(); y += 4. * scale) {
			p.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
		}
		p.setClipping(false);
		auto edge = QLinearGradient(rect.topLeft(), rect.topRight());
		edge.setColorAt(0., WithAlpha(accent, 200));
		edge.setColorAt(0.5, QColor(170, 90, 255, 170));
		edge.setColorAt(1., QColor(0, 230, 255, 190));
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(edge), 1.4 * scale));
		p.drawPath(path);
	} break;
	case Style::Neon: {
		p.setBrush(Mix(base, QColor(0, 0, 0), 0.25));
		p.drawPath(path);
		for (auto i = 3; i != 0; --i) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(WithAlpha(accent, 40 * (4 - i)), (1. + i * 1.4) * scale));
			p.drawPath(path);
		}
		p.setPen(QPen(accent, 1.5 * scale));
		p.drawPath(path);
		const auto stripe = QRectF(
			rect.left() + 1.5 * scale,
			rect.top() + 12. * scale,
			3.5 * scale,
			rect.height() - 24. * scale);
		p.setPen(Qt::NoPen);
		p.setBrush(accent);
		p.drawRoundedRect(stripe, 2. * scale, 2. * scale);
	} break;
	case Style::Block: {
		const auto depth = 4. * scale;
		auto face = rect.adjusted(0., 0., 0., -depth);
		auto facePath = QPainterPath();
		facePath.addRoundedRect(face, radius, radius);
		auto sidePath = QPainterPath();
		sidePath.addRoundedRect(face.translated(0., depth), radius, radius);
		p.setBrush(Mix(accent, QColor(0, 0, 0), 0.6));
		p.drawPath(sidePath);
		auto gradient = QLinearGradient(face.topLeft(), face.bottomLeft());
		gradient.setColorAt(0., Mix(base, accent, 0.42));
		gradient.setColorAt(1., Mix(base, accent, 0.26));
		p.setBrush(gradient);
		p.drawPath(facePath);
		auto gloss = QLinearGradient(
			face.topLeft(),
			QPointF(face.left(), face.top() + face.height() * 0.5));
		gloss.setColorAt(0., QColor(255, 255, 255, 46));
		gloss.setColorAt(1., QColor(255, 255, 255, 0));
		p.setBrush(gloss);
		p.drawPath(facePath);
	} break;
	case Style::Plain:
		break;
	}
	p.restore();
}

void PaintShimmer(QPainter &p, QSize size, double phase) {
	if (Current() != Style::Hologram) {
		return;
	}
	const auto rect = CardRect(size);
	const auto scale = Scale();
	auto path = QPainterPath();
	path.addRoundedRect(rect, 18. * scale, 18. * scale);
	const auto width = rect.width();
	const auto x = rect.left() - width * 0.4 + phase * width * 1.8;
	auto band = QLinearGradient(QPointF(x, rect.top()), QPointF(x + width * 0.35, rect.bottom()));
	band.setColorAt(0., QColor(255, 255, 255, 0));
	band.setColorAt(0.5, QColor(255, 255, 255, 30));
	band.setColorAt(1., QColor(255, 255, 255, 0));
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setClipPath(path);
	p.fillRect(rect, band);
	p.restore();
}

} // namespace Mwtk::Notify
