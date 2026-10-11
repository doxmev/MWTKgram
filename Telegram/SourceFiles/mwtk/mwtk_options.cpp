/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_options.h"

#include "base/options.h"

#include <algorithm>

namespace Mwtk {
namespace {

base::options::toggle OptionVisuals({
	.id = kOptionVisuals,
	.name = "MWTKgram visuals",
	.description = "Animated background visuals.",
	.defaultValue = true,
});

base::options::toggle OptionWatermark({
	.id = kOptionWatermark,
	.name = "MWTKgram watermark",
	.description = "Floating doxmev label.",
	.defaultValue = true,
});

base::options::toggle OptionObjects({
	.id = kOptionObjects,
	.name = "MWTKgram 3D objects",
	.description = "Floating 3D wireframe objects.",
	.defaultValue = true,
});

base::options::toggle OptionParticles({
	.id = kOptionParticles,
	.name = "MWTKgram particles",
	.description = "Floating glowing particles.",
	.defaultValue = true,
});

base::options::option<int> OptionPalette({
	.id = kOptionPalette,
	.name = "MWTKgram palette",
	.description = "Visuals color palette.",
	.defaultValue = 0,
});

base::options::option<int> OptionIntensity({
	.id = kOptionIntensity,
	.name = "MWTKgram intensity",
	.description = "Visuals brightness.",
	.defaultValue = 1,
});

base::options::option<int> OptionRoom({
	.id = kOptionRoom,
	.name = "MWTKgram 3D room",
	.description = "3D room behind chats.",
	.defaultValue = 0,
});

base::options::option<int> OptionRoomYaw({
	.id = kOptionRoomYaw,
	.name = "MWTKgram room yaw",
	.description = "Camera rotation in the 3D room.",
	.defaultValue = 0,
});

base::options::option<int> OptionRoomPitch({
	.id = kOptionRoomPitch,
	.name = "MWTKgram room pitch",
	.description = "Camera tilt in the 3D room.",
	.defaultValue = 2,
});

base::options::option<int> OptionRoomFov({
	.id = kOptionRoomFov,
	.name = "MWTKgram room field of view",
	.description = "Camera field of view in the 3D room.",
	.defaultValue = 75,
});

base::options::toggle OptionRoomParallax({
	.id = kOptionRoomParallax,
	.name = "MWTKgram room parallax",
	.description = "Camera follows the mouse.",
	.defaultValue = true,
});

base::options::toggle OptionRoomMotion({
	.id = kOptionRoomMotion,
	.name = "MWTKgram room motion",
	.description = "Camera sway and drifting fog.",
	.defaultValue = false,
});

base::options::toggle OptionEffects({
	.id = kOptionEffects,
	.name = "MWTKgram effects",
	.description = "Master switch for all MWTKgram effects (F9).",
	.defaultValue = true,
});

base::options::toggle OptionBubbles3d({
	.id = kOptionBubbles3d,
	.name = "MWTKgram 3D message bubbles",
	.description = "Volumetric message bubbles.",
	.defaultValue = true,
});

base::options::toggle OptionBubblesTint({
	.id = kOptionBubblesTint,
	.name = "MWTKgram tinted message bubbles",
	.description = "Message bubbles follow the MWTKgram color.",
	.defaultValue = true,
});

base::options::option<int> OptionBubblesOpacity({
	.id = kOptionBubblesOpacity,
	.name = "MWTKgram message bubbles opacity",
	.description = "Message bubbles translucency.",
	.defaultValue = 1,
});

base::options::toggle OptionCards3d({
	.id = kOptionCards3d,
	.name = "MWTKgram 3D chat list",
	.description = "Chat list rows as volumetric cards.",
	.defaultValue = true,
});

base::options::option<int> OptionQuality({
	.id = kOptionQuality,
	.name = "MWTKgram graphics quality",
	.description = "Frame rate and scene detail.",
	.defaultValue = 1,
});

base::options::toggle OptionBackgroundAnimation({
	.id = kOptionBackgroundAnimation,
	.name = "MWTKgram background animation",
	.description = "Keep animating while the window is not active.",
	.defaultValue = false,
});

base::options::toggle OptionUiScene({
	.id = kOptionUiScene,
	.name = "MWTKgram interface in scene",
	.description = "Interface framed inside the 3D scene.",
	.defaultValue = false,
});

base::options::toggle OptionButtons3d({
	.id = kOptionButtons3d,
	.name = "MWTKgram 3D buttons",
	.description = "Volumetric buttons.",
	.defaultValue = true,
});

base::options::toggle OptionButtonsAnimated({
	.id = kOptionButtonsAnimated,
	.name = "MWTKgram animated buttons",
	.description = "Animated glowing button highlight.",
	.defaultValue = true,
});

} // namespace

const char kOptionVisuals[] = "mwtk-visuals";
const char kOptionWatermark[] = "mwtk-watermark";
const char kOptionObjects[] = "mwtk-objects";
const char kOptionParticles[] = "mwtk-particles";
const char kOptionPalette[] = "mwtk-palette";
const char kOptionIntensity[] = "mwtk-intensity";
const char kOptionRoom[] = "mwtk-room";
const char kOptionRoomYaw[] = "mwtk-room-yaw";
const char kOptionRoomPitch[] = "mwtk-room-pitch";
const char kOptionRoomFov[] = "mwtk-room-fov";
const char kOptionRoomParallax[] = "mwtk-room-parallax";
const char kOptionRoomMotion[] = "mwtk-room-motion";
const char kOptionUiScene[] = "mwtk-ui-scene";
const char kOptionButtons3d[] = "mwtk-buttons-3d";
const char kOptionButtonsAnimated[] = "mwtk-buttons-animated";
const char kOptionQuality[] = "mwtk-quality";
const char kOptionEffects[] = "mwtk-effects";
const char kOptionBubbles3d[] = "mwtk-bubbles-3d";
const char kOptionBubblesTint[] = "mwtk-bubbles-tint";
const char kOptionBubblesOpacity[] = "mwtk-bubbles-opacity";
const char kOptionCards3d[] = "mwtk-cards-3d";
const char kOptionBackgroundAnimation[] = "mwtk-background-animation";

QString QualityName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Низкое");
	case 2: return QString::fromUtf8("Высокое");
	}
	return QString::fromUtf8("Среднее");
}

Quality CurrentQuality() {
	switch (std::clamp(OptionQuality.value(), 0, kQualityCount - 1)) {
	case 0: return { 50, 0.45, 0.5, 160, false };
	case 2: return { 16, 1., 1., 460, true };
	}
	return { 33, 0.75, 0.7, 300, false };
}

QString IntensityName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Слабо");
	case 2: return QString::fromUtf8("Ярко");
	}
	return QString::fromUtf8("Средне");
}

double IntensityOpacity(int index) {
	switch (index) {
	case 0: return 0.3;
	case 2: return 0.9;
	}
	return 0.55;
}

bool EffectsEnabled() {
	return OptionEffects.value();
}

void ToggleEffects() {
	OptionEffects.set(!OptionEffects.value());
}

bool VisualsEnabled() {
	return OptionEffects.value()
		&& OptionVisuals.value()
		&& (OptionWatermark.value()
			|| OptionObjects.value()
			|| OptionParticles.value());
}

SceneConfig CurrentConfig(double scale) {
	auto result = SceneConfig();
	result.text = OptionWatermark.value();
	result.objects = OptionObjects.value();
	result.particles = OptionParticles.value();
	result.palette = std::clamp(OptionPalette.value(), 0, PalettesCount() - 1);
	result.opacity = IntensityOpacity(
		std::clamp(OptionIntensity.value(), 0, kIntensityCount - 1));
	result.scale = scale;
	result.density = CurrentQuality().density;
	return result;
}

rpl::producer<> ConfigChanges() {
	return rpl::merge(
		OptionEffects.changes(),
		OptionQuality.changes(),
		OptionBackgroundAnimation.changes(),
		OptionVisuals.changes(),
		OptionWatermark.changes(),
		OptionObjects.changes(),
		OptionParticles.changes(),
		OptionPalette.changes(),
		OptionIntensity.changes());
}

rpl::producer<> RoomConfigChanges() {
	return rpl::merge(
		OptionEffects.changes(),
		OptionRoom.changes(),
		OptionRoomYaw.changes(),
		OptionRoomPitch.changes(),
		OptionRoomFov.changes(),
		OptionRoomParallax.changes(),
		OptionRoomMotion.changes(),
		OptionQuality.changes(),
		OptionPalette.changes());
}

QString BubbleOpacityName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Плотные");
	case 2: return QString::fromUtf8("Стекло");
	}
	return QString::fromUtf8("Полупрозрачные");
}

CardStyle CurrentCardStyle() {
	auto result = CardStyle();
	result.enabled = OptionEffects.value() && OptionCards3d.value();
	result.accent = PaletteAt(
		std::clamp(OptionPalette.value(), 0, PalettesCount() - 1),
		0.).first;
	return result;
}

rpl::producer<> ChatStyleChanges() {
	return rpl::merge(
		OptionEffects.changes(),
		OptionBubbles3d.changes(),
		OptionBubblesTint.changes(),
		OptionBubblesOpacity.changes(),
		OptionCards3d.changes(),
		OptionPalette.changes());
}

rpl::producer<> InterfaceConfigChanges() {
	return rpl::merge(
		OptionEffects.changes(),
		OptionUiScene.changes(),
		OptionButtons3d.changes(),
		OptionButtonsAnimated.changes(),
		OptionRoom.changes(),
		OptionPalette.changes());
}

bool OptionValue(const char id[]) {
	return base::options::value<bool>(id);
}

int OptionInt(const char id[]) {
	return base::options::value<int>(id);
}

} // namespace Mwtk
