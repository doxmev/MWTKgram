/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Mwtk::Notify {

enum class Style {
	Plain,
	Hologram,
	Neon,
	Block,
};

inline constexpr auto kStyleCount = 4;
[[nodiscard]] QString StyleName(int index);

// Current style, Plain when MWTK effects are off.
[[nodiscard]] Style Current();

// Rounded shape of the card inside a notification of this size.
[[nodiscard]] QRectF CardRect(QSize size);

// Paints the card background (only inside clip when it is not empty).
void PaintBackground(QPainter &p, QSize size, QRect clip = QRect());

// Moving light over the card, phase in [0, 1). Hologram only.
void PaintShimmer(QPainter &p, QSize size, double phase);

} // namespace Mwtk::Notify
