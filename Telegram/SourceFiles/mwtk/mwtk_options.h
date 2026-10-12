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
extern const char kOptionQuality[];
extern const char kOptionEffects[];
extern const char kOptionBubbles3d[];
extern const char kOptionBubblesTint[];
extern const char kOptionBubblesOpacity[];
extern const char kOptionCards3d[];
extern const char kOptionBackgroundAnimation[];
extern const char kOptionSceneBase[];
extern const char kOptionSceneTime[];
extern const char kOptionSceneDensity[];
extern const char kOptionSceneFog[];
extern const char kOptionSceneNotes[];
extern const char kOptionBackgroundPhoto[];
extern const char kOptionBackgroundVideo[];
extern const char kOptionBackgroundDim[];
extern const char kOptionCursorTrail[];
extern const char kOptionUiSounds[];
extern const char kOptionUiSoundsVolume[];
extern const char kOptionSendEffect[];
extern const char kOptionMessageAppear[];
extern const char kOptionChatListOpacity[];
extern const char kOptionAvatarGlow[];
extern const char kOptionAvatarOnlineColor[];
extern const char kOptionAvatarOfflineColor[];
extern const char kOptionCustomColor[];
extern const char kOptionNightEvents[];
extern const char kOptionSaveDeleted[];

inline constexpr auto kCustomPaletteIndex = 7;

// Alpha of the chat list background, 1. when it is opaque.
[[nodiscard]] double ChatListAlpha();
[[nodiscard]] bool ChatListTransparent();
[[nodiscard]] rpl::producer<> ChatListChanges();

struct AvatarGlow {
	bool enabled = false;
	QColor online;
	QColor offline;
};
[[nodiscard]] AvatarGlow CurrentAvatarGlow();

// True between 3:00 and 3:59 local time when night events are enabled.
[[nodiscard]] bool NightEventActive();

inline constexpr auto kPresetCount = 5;
[[nodiscard]] QString PresetName(int index);
void ApplyPreset(int index);

inline constexpr auto kBackgroundCount = 6;
inline constexpr auto kSceneBaseCount = 5;
inline constexpr auto kSceneTimeCount = 3;
[[nodiscard]] QString BackgroundName(int index);
[[nodiscard]] QString SceneBaseName(int index);
[[nodiscard]] QString SceneTimeName(int index);

inline constexpr auto kIntensityCount = 3;

inline constexpr auto kQualityCount = 3;

struct Quality {
	int frameDelay = 33;
	double density = 0.75;
	double roomScale = 0.7;
	int trees = 300;
	bool decorativeMotion = false;
};

[[nodiscard]] QString QualityName(int index);
[[nodiscard]] Quality CurrentQuality();

[[nodiscard]] QString IntensityName(int index);
[[nodiscard]] double IntensityOpacity(int index);

[[nodiscard]] bool EffectsEnabled();
void ToggleEffects();
[[nodiscard]] bool VisualsEnabled();
[[nodiscard]] SceneConfig CurrentConfig(double scale);
[[nodiscard]] rpl::producer<> ConfigChanges();
[[nodiscard]] rpl::producer<> RoomConfigChanges();
[[nodiscard]] rpl::producer<> ChatStyleChanges();

inline constexpr auto kBubbleOpacityCount = 3;
[[nodiscard]] QString BubbleOpacityName(int index);

struct CardStyle {
	bool enabled = false;
	QColor accent;
	double alpha = 1.;
};
[[nodiscard]] CardStyle CurrentCardStyle();
[[nodiscard]] rpl::producer<> InterfaceConfigChanges();

[[nodiscard]] bool OptionValue(const char id[]);
[[nodiscard]] int OptionInt(const char id[]);
[[nodiscard]] QString OptionString(const char id[]);

} // namespace Mwtk
