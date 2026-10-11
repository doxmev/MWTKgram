/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_options.h"

#include "base/options.h"

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

} // namespace

const char kOptionVisuals[] = "mwtk-visuals";
const char kOptionWatermark[] = "mwtk-watermark";
const char kOptionObjects[] = "mwtk-objects";
const char kOptionParticles[] = "mwtk-particles";
const char kOptionPalette[] = "mwtk-palette";
const char kOptionIntensity[] = "mwtk-intensity";

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

} // namespace Mwtk
