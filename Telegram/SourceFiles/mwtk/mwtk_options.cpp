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
	.defaultValue = true,
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

bool VisualsEnabled() {
	return OptionVisuals.value()
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
	return result;
}

rpl::producer<> ConfigChanges() {
	return rpl::merge(
		OptionVisuals.changes(),
		OptionWatermark.changes(),
		OptionObjects.changes(),
		OptionParticles.changes(),
		OptionPalette.changes(),
		OptionIntensity.changes());
}

rpl::producer<> RoomConfigChanges() {
	return rpl::merge(
		OptionRoom.changes(),
		OptionRoomYaw.changes(),
		OptionRoomPitch.changes(),
		OptionRoomFov.changes(),
		OptionRoomParallax.changes(),
		OptionRoomMotion.changes(),
		OptionPalette.changes());
}

rpl::producer<> InterfaceConfigChanges() {
	return rpl::merge(
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
