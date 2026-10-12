/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Mwtk::AppIcon {

inline constexpr auto kCount = 4;

[[nodiscard]] QString Name(int index);

// Subscribes to the icon option and refreshes window, taskbar and tray icons.
void Setup();

} // namespace Mwtk::AppIcon
