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
extern const char kOptionRoom[];
extern const char kOptionRoomYaw[];
extern const char kOptionRoomPitch[];
extern const char kOptionRoomFov[];
extern const char kOptionRoomParallax[];
extern const char kOptionRoomMotion[];
extern const char kOptionUiScene[];
extern const char kOptionButtons3d[];
extern const char kOptionButtonsAnimated[];

inline constexpr auto kIntensityCount = 3;

[[nodiscard]] QString IntensityName(int index);
[[nodiscard]] double IntensityOpacity(int index);

[[nodiscard]] bool VisualsEnabled();
[[nodiscard]] SceneConfig CurrentConfig(double scale);
[[nodiscard]] rpl::producer<> ConfigChanges();
[[nodiscard]] rpl::producer<> RoomConfigChanges();
[[nodiscard]] rpl::producer<> InterfaceConfigChanges();

[[nodiscard]] bool OptionValue(const char id[]);
[[nodiscard]] int OptionInt(const char id[]);

} // namespace Mwtk
