/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mwtk/mwtk_scene.h"

#include <rpl/producer.h>

namespace Mwtk {

extern const char kOptionVisuals[];
extern const char kOptionWatermark[];
extern const char kOptionObjects[];
extern const char kOptionParticles[];
extern const char kOptionPalette[];
extern const char kOptionIntensity[];

inline constexpr auto kIntensityCount = 3;

[[nodiscard]] QString IntensityName(int index);
[[nodiscard]] double IntensityOpacity(int index);

[[nodiscard]] bool VisualsEnabled();
[[nodiscard]] SceneConfig CurrentConfig(double scale);
[[nodiscard]] rpl::producer<> ConfigChanges();

} // namespace Mwtk
