/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_options.h"

#include "base/timer.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

#include "base/options.h"

#include <QtCore/QTime>

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

base::options::option<int> OptionSceneBase({
	.id = kOptionSceneBase,
	.name = "MWTKgram custom scene base",
	.description = "Base of the custom 3D scene: forest, neon or space.",
	.defaultValue = 0,
});

base::options::option<int> OptionSceneTime({
	.id = kOptionSceneTime,
	.name = "MWTKgram custom scene time",
	.description = "Time of day in the custom 3D scene.",
	.defaultValue = 0,
});

base::options::option<int> OptionSceneDensity({
	.id = kOptionSceneDensity,
	.name = "MWTKgram custom scene density",
	.description = "Amount of trees, lines or stars in the custom scene.",
	.defaultValue = 70,
});

base::options::option<int> OptionSceneFog({
	.id = kOptionSceneFog,
	.name = "MWTKgram custom scene fog",
	.description = "Fog or glow strength in the custom scene.",
	.defaultValue = 50,
});

base::options::option<QString> OptionSceneNotes({
	.id = kOptionSceneNotes,
	.name = "MWTKgram custom scene notes",
	.description = "Texts of the notes on the trees, one per line.",
	.defaultValue = QString(),
});

base::options::option<QString> OptionBackgroundPhoto({
	.id = kOptionBackgroundPhoto,
	.name = "MWTKgram background photo",
	.description = "Path of the custom background photo.",
	.defaultValue = QString(),
});

base::options::option<QString> OptionBackgroundVideo({
	.id = kOptionBackgroundVideo,
	.name = "MWTKgram background video",
	.description = "Path of the custom background video.",
	.defaultValue = QString(),
});

base::options::option<int> OptionBackgroundDim({
	.id = kOptionBackgroundDim,
	.name = "MWTKgram background dimming",
	.description = "Darkening of the photo or video background, percent.",
	.defaultValue = 30,
});

base::options::toggle OptionCursorTrail({
	.id = kOptionCursorTrail,
	.name = "MWTKgram cursor trail",
	.description = "Neon trail behind the mouse cursor.",
	.defaultValue = true,
});

base::options::toggle OptionUiSounds({
	.id = kOptionUiSounds,
	.name = "MWTKgram interface sounds",
	.description = "Soft sounds for clicks, sending and F9.",
	.defaultValue = true,
});

base::options::option<int> OptionUiSoundsVolume({
	.id = kOptionUiSoundsVolume,
	.name = "MWTKgram interface sounds volume",
	.description = "Volume of the interface sounds, percent.",
	.defaultValue = 30,
});

base::options::toggle OptionSendEffect({
	.id = kOptionSendEffect,
	.name = "MWTKgram send effect",
	.description = "Particle burst when a message is sent.",
	.defaultValue = true,
});

base::options::toggle OptionMessageAppear({
	.id = kOptionMessageAppear,
	.name = "MWTKgram message appear animation",
	.description = "New messages slide in smoothly.",
	.defaultValue = true,
});

base::options::option<int> OptionChatListOpacity({
	.id = kOptionChatListOpacity,
	.name = "MWTKgram chat list opacity",
	.description = "Opacity of the chat list over the background, percent.",
	.defaultValue = 100,
});

base::options::toggle OptionAvatarGlow({
	.id = kOptionAvatarGlow,
	.name = "MWTKgram glowing avatars",
	.description = "Neon ring around avatars in the chat list.",
	.defaultValue = true,
});

base::options::option<int> OptionAvatarOnlineColor({
	.id = kOptionAvatarOnlineColor,
	.name = "MWTKgram online ring color",
	.description = "ARGB color of the avatar ring for online users.",
	.defaultValue = int(0xff39ff88u),
});

base::options::option<int> OptionAvatarOfflineColor({
	.id = kOptionAvatarOfflineColor,
	.name = "MWTKgram offline ring color",
	.description = "ARGB color of the avatar ring, 0 for the theme color.",
	.defaultValue = 0,
});

base::options::option<int> OptionCustomColor({
	.id = kOptionCustomColor,
	.name = "MWTKgram custom color",
	.description = "ARGB color of the custom palette.",
	.defaultValue = int(0xff00e5ffu),
});

base::options::toggle OptionNightEvents({
	.id = kOptionNightEvents,
	.name = "MWTKgram night events",
	.description = "Secrets that appear at 3 AM.",
	.defaultValue = true,
});

base::options::toggle OptionSaveDeleted({
	.id = kOptionSaveDeleted,
	.name = "MWTKgram save deleted messages",
	.description = "Keep a local copy of deleted messages.",
	.defaultValue = true,
});

base::options::toggle OptionKeepDeletedInChat({
	.id = kOptionKeepDeletedInChat,
	.name = "MWTKgram keep deleted messages in chat",
	.description = "Deleted messages stay in the chat with a mark.",
	.defaultValue = true,
});

base::options::toggle OptionGpu({
	.id = kOptionGpu,
	.name = "MWTKgram GPU rendering",
	.description = "Draw MWTK effects on the video card.",
	.defaultValue = false,
});

base::options::toggle OptionLaptopMode({
	.id = kOptionLaptopMode,
	.name = "MWTKgram laptop mode",
	.description = "Use the low preset while running on battery.",
	.defaultValue = true,
});

base::options::option<int> OptionAppIcon({
	.id = kOptionAppIcon,
	.name = "MWTKgram app icon",
	.description = "Index of the application icon.",
	.defaultValue = 0,
});

base::options::option<int> OptionNotifyStyle({
	.id = kOptionNotifyStyle,
	.name = "MWTKgram notification style",
	.description = "0 plain, 1 hologram, 2 neon, 3 block.",
	.defaultValue = 1,
});

base::options::toggle OptionOnlineStripe({
	.id = kOptionOnlineStripe,
	.name = "MWTKgram online stripe",
	.description = "Glowing stripe on cards of online chats.",
	.defaultValue = false,
});

base::options::toggle OptionOnlineCardGlow({
	.id = kOptionOnlineCardGlow,
	.name = "MWTKgram online card glow",
	.description = "Cards of online chats glow.",
	.defaultValue = false,
});

base::options::toggle OptionOnlinePulse({
	.id = kOptionOnlinePulse,
	.name = "MWTKgram online pulse",
	.description = "The online dot breathes.",
	.defaultValue = false,
});

base::options::toggle OptionOnlineTile({
	.id = kOptionOnlineTile,
	.name = "MWTKgram online tile",
	.description = "Rounded-square userpics for online users.",
	.defaultValue = false,
});

base::options::toggle OptionOnlineDot({
	.id = kOptionOnlineDot,
	.name = "MWTKgram online dot",
	.description = "Show the online dot.",
	.defaultValue = true,
});

base::options::toggle OptionLyrics({
	.id = kOptionLyrics,
	.name = "MWTKgram lyrics",
	.description = "Flying synced lyrics while music plays.",
	.defaultValue = true,
});

base::options::toggle OptionLyricsOnline({
	.id = kOptionLyricsOnline,
	.name = "MWTKgram lyrics search",
	.description = "Look up synced lyrics in LRCLIB.",
	.defaultValue = true,
});

base::options::option<int> OptionLyricsLayout({
	.id = kOptionLyricsLayout,
	.name = "MWTKgram lyrics layout",
	.description = "0 words, 1 cascade, 2 line.",
	.defaultValue = 0,
});

base::options::option<int> OptionLyricsVanish({
	.id = kOptionLyricsVanish,
	.name = "MWTKgram lyrics vanish",
	.description = "0 sparks, 1 fade, 2 fly away.",
	.defaultValue = 0,
});

base::options::option<int> OptionLyricsStyle({
	.id = kOptionLyricsStyle,
	.name = "MWTKgram lyrics style",
	.description = "0 neon, 1 3D, 2 pixels.",
	.defaultValue = 0,
});

base::options::option<int> OptionLyricsMotion({
	.id = kOptionLyricsMotion,
	.name = "MWTKgram lyrics motion",
	.description = "Word drift, percent.",
	.defaultValue = 60,
});

base::options::option<int> OptionLyricsSize({
	.id = kOptionLyricsSize,
	.name = "MWTKgram lyrics size",
	.description = "Text size, percent.",
	.defaultValue = 100,
});

base::options::toggle OptionHideChats({
	.id = kOptionHideChats,
	.name = "MWTKgram hide chats for lyrics",
	.description = "Hide the interface while lyrics play.",
	.defaultValue = false,
});

base::options::toggle OptionMusicPulse({
	.id = kOptionMusicPulse,
	.name = "MWTKgram music pulse",
	.description = "Background reacts to the beat.",
	.defaultValue = true,
});

base::options::toggle OptionSeasons({
	.id = kOptionSeasons,
	.name = "MWTKgram seasons",
	.description = "Snow, leaves and holidays in scenes.",
	.defaultValue = true,
});

base::options::toggle OptionFlyby({
	.id = kOptionFlyby,
	.name = "MWTKgram message fly-by",
	.description = "Userpic flies over the background on new messages.",
	.defaultValue = true,
});

base::options::toggle OptionFlybyFavorites({
	.id = kOptionFlybyFavorites,
	.name = "MWTKgram fly-by only favorites",
	.description = "Fly-by only for pinned chats.",
	.defaultValue = false,
});

base::options::toggle OptionHoloProfile({
	.id = kOptionHoloProfile,
	.name = "MWTKgram holographic profile",
	.description = "Hologram effect on profile cards.",
	.defaultValue = true,
});

base::options::toggle OptionQuickPhrases({
	.id = kOptionQuickPhrases,
	.name = "MWTKgram quick phrases",
	.description = "Suggest saved phrases after typing a slash.",
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
const char kOptionSceneBase[] = "mwtk-scene-base";
const char kOptionSceneTime[] = "mwtk-scene-time";
const char kOptionSceneDensity[] = "mwtk-scene-density";
const char kOptionSceneFog[] = "mwtk-scene-fog";
const char kOptionSceneNotes[] = "mwtk-scene-notes";
const char kOptionBackgroundPhoto[] = "mwtk-background-photo";
const char kOptionBackgroundVideo[] = "mwtk-background-video";
const char kOptionBackgroundDim[] = "mwtk-background-dim";
const char kOptionCursorTrail[] = "mwtk-cursor-trail";
const char kOptionUiSounds[] = "mwtk-ui-sounds";
const char kOptionUiSoundsVolume[] = "mwtk-ui-sounds-volume";
const char kOptionSendEffect[] = "mwtk-send-effect";
const char kOptionMessageAppear[] = "mwtk-message-appear";
const char kOptionChatListOpacity[] = "mwtk-chat-list-opacity";
const char kOptionAvatarGlow[] = "mwtk-avatar-glow";
const char kOptionAvatarOnlineColor[] = "mwtk-avatar-online-color";
const char kOptionAvatarOfflineColor[] = "mwtk-avatar-offline-color";
const char kOptionCustomColor[] = "mwtk-custom-color";
const char kOptionNightEvents[] = "mwtk-night-events";
const char kOptionSaveDeleted[] = "mwtk-save-deleted";
const char kOptionKeepDeletedInChat[] = "mwtk-keep-deleted-in-chat";
const char kOptionGpu[] = "mwtk-gpu";
const char kOptionLaptopMode[] = "mwtk-laptop-mode";
const char kOptionAppIcon[] = "mwtk-app-icon";
const char kOptionNotifyStyle[] = "mwtk-notify-style";
const char kOptionOnlineStripe[] = "mwtk-online-stripe";
const char kOptionOnlineCardGlow[] = "mwtk-online-card-glow";
const char kOptionOnlinePulse[] = "mwtk-online-pulse";
const char kOptionOnlineTile[] = "mwtk-online-tile";
const char kOptionOnlineDot[] = "mwtk-online-dot";
const char kOptionLyrics[] = "mwtk-lyrics";
const char kOptionLyricsOnline[] = "mwtk-lyrics-online";
const char kOptionLyricsLayout[] = "mwtk-lyrics-layout";
const char kOptionLyricsVanish[] = "mwtk-lyrics-vanish";
const char kOptionLyricsStyle[] = "mwtk-lyrics-style";
const char kOptionLyricsMotion[] = "mwtk-lyrics-motion";
const char kOptionLyricsSize[] = "mwtk-lyrics-size";
const char kOptionHideChats[] = "mwtk-hide-chats";
const char kOptionMusicPulse[] = "mwtk-music-pulse";
const char kOptionSeasons[] = "mwtk-seasons";
const char kOptionFlyby[] = "mwtk-flyby";
const char kOptionFlybyFavorites[] = "mwtk-flyby-favorites";
const char kOptionHoloProfile[] = "mwtk-holo-profile";
const char kOptionQuickPhrases[] = "mwtk-quick-phrases";

QString QualityName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Низкое");
	case 2: return QString::fromUtf8("Высокое");
	}
	return QString::fromUtf8("Среднее");
}

namespace {

[[nodiscard]] bool ReadOnBattery() {
#ifdef Q_OS_WIN
	auto status = SYSTEM_POWER_STATUS();
	if (GetSystemPowerStatus(&status)) {
		// 0 = offline (battery), 1 = online, 255 = unknown.
		// BatteryFlag 128 = no system battery (desktop PC).
		return (status.ACLineStatus == 0) && (status.BatteryFlag != 128);
	}
#endif // Q_OS_WIN
	return false;
}

struct PowerWatcher {
	bool onBattery = false;
	rpl::event_stream<> changes;
	std::unique_ptr<base::Timer> timer;
};

[[nodiscard]] PowerWatcher &Power() {
	static auto result = PowerWatcher();
	if (!result.timer) {
		result.onBattery = ReadOnBattery();
		result.timer = std::make_unique<base::Timer>([] {
			auto &power = Power();
			const auto now = ReadOnBattery();
			if (power.onBattery != now) {
				power.onBattery = now;
				power.changes.fire({});
			}
		});
		result.timer->callEach(20 * crl::time(1000));
	}
	return result;
}

} // namespace

bool LaptopSavingActive() {
	return OptionLaptopMode.value() && Power().onBattery;
}

rpl::producer<> LaptopSavingChanges() {
	return rpl::merge(
		OptionLaptopMode.changes(),
		Power().changes.events());
}

Quality CurrentQuality() {
	if (LaptopSavingActive()) {
		return { 66, 0.4, 0.5, 160, false };
	}
	switch (std::clamp(OptionQuality.value(), 0, kQualityCount - 1)) {
	case 0: return { 66, 0.4, 0.5, 160, false };
	case 2: return { 20, 1., 1., 460, true };
	}
	return { 40, 0.7, 0.7, 300, false };
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
	const auto quality = CurrentQuality();
	result.density = quality.density;
	result.lite = !quality.decorativeMotion;
	return result;
}

rpl::producer<> ConfigChanges() {
	return rpl::merge(
		LaptopSavingChanges(),
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
		LaptopSavingChanges(),
		OptionNightEvents.changes(),
		OptionEffects.changes(),
		OptionRoom.changes(),
		OptionRoomYaw.changes(),
		OptionRoomPitch.changes(),
		OptionRoomFov.changes(),
		OptionRoomParallax.changes(),
		OptionRoomMotion.changes(),
		OptionQuality.changes(),
		OptionPalette.changes(),
		OptionSceneBase.changes(),
		OptionSceneTime.changes(),
		OptionSceneDensity.changes(),
		OptionSceneFog.changes(),
		OptionSceneNotes.changes(),
		OptionBackgroundPhoto.changes(),
		OptionBackgroundVideo.changes(),
		OptionBackgroundDim.changes());
}

QString BackgroundName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Выкл");
	case 1: return QString::fromUtf8("Лес");
	case 2: return QString::fromUtf8("Неон");
	case 3: return QString::fromUtf8("Своя");
	case 4: return QString::fromUtf8("Фото");
	case 5: return QString::fromUtf8("Видео");
	}
	return QString();
}

QString SceneBaseName(int index) {
	switch (index) {
	case 1: return QString::fromUtf8("Неон");
	case 2: return QString::fromUtf8("Космос");
	case 3: return QString::fromUtf8("Город");
	case 4: return QString::fromUtf8("Под водой");
	}
	return QString::fromUtf8("Лес");
}

QString SceneTimeName(int index) {
	switch (index) {
	case 1: return QString::fromUtf8("Закат");
	case 2: return QString::fromUtf8("День");
	}
	return QString::fromUtf8("Ночь");
}

QString PresetName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Хоррор");
	case 1: return QString::fromUtf8("Синтвейв");
	case 2: return QString::fromUtf8("Космос");
	case 3: return QString::fromUtf8("Эконом");
	case 4: return QString::fromUtf8("Максимум");
	}
	return QString();
}

void ApplyPreset(int index) {
	OptionEffects.set(true);
	switch (index) {
	case 0: // Horror forest.
		OptionPalette.set(0);
		OptionRoom.set(1);
		OptionQuality.set(1);
		OptionVisuals.set(true);
		OptionWatermark.set(true);
		OptionObjects.set(false);
		OptionParticles.set(true);
		OptionIntensity.set(1);
		OptionRoomMotion.set(true);
		OptionBubblesOpacity.set(1);
		break;
	case 1: // Synthwave.
		OptionPalette.set(1);
		OptionRoom.set(2);
		OptionQuality.set(1);
		OptionVisuals.set(true);
		OptionWatermark.set(true);
		OptionObjects.set(true);
		OptionParticles.set(true);
		OptionIntensity.set(2);
		OptionRoomMotion.set(false);
		OptionBubblesOpacity.set(2);
		break;
	case 2: // Space.
		OptionPalette.set(4);
		OptionRoom.set(3);
		OptionSceneBase.set(2);
		OptionSceneDensity.set(70);
		OptionSceneFog.set(60);
		OptionQuality.set(1);
		OptionVisuals.set(true);
		OptionWatermark.set(true);
		OptionObjects.set(true);
		OptionParticles.set(true);
		OptionIntensity.set(1);
		OptionRoomMotion.set(false);
		OptionBubblesOpacity.set(2);
		break;
	case 3: // Economy.
		OptionQuality.set(0);
		OptionRoom.set(0);
		OptionVisuals.set(false);
		OptionRoomMotion.set(false);
		OptionButtonsAnimated.set(false);
		OptionUiScene.set(false);
		OptionCursorTrail.set(false);
		OptionSendEffect.set(false);
		OptionBackgroundAnimation.set(false);
		break;
	case 4: // Maximum.
		OptionQuality.set(2);
		OptionRoom.set(1);
		OptionVisuals.set(true);
		OptionWatermark.set(true);
		OptionObjects.set(true);
		OptionParticles.set(true);
		OptionIntensity.set(2);
		OptionRoomMotion.set(true);
		OptionButtons3d.set(true);
		OptionButtonsAnimated.set(true);
		OptionUiScene.set(true);
		OptionCursorTrail.set(true);
		OptionSendEffect.set(true);
		OptionBubbles3d.set(true);
		OptionCards3d.set(true);
		break;
	}
}

QString BubbleOpacityName(int index) {
	switch (index) {
	case 0: return QString::fromUtf8("Плотные");
	case 2: return QString::fromUtf8("Стекло");
	}
	return QString::fromUtf8("Полупрозрачные");
}

double ChatListAlpha() {
	if (!OptionEffects.value() || OptionRoom.value() <= 0) {
		return 1.;
	}
	return std::clamp(OptionChatListOpacity.value(), 10, 100) / 100.;
}

bool ChatListTransparent() {
	return ChatListAlpha() < 0.995;
}

rpl::producer<> ChatListChanges() {
	return rpl::merge(
		OptionOnlineStripe.changes(),
		OptionOnlineCardGlow.changes(),
		OptionOnlinePulse.changes(),
		OptionOnlineTile.changes(),
		OptionOnlineDot.changes(),
		OptionEffects.changes(),
		OptionRoom.changes(),
		OptionChatListOpacity.changes(),
		OptionAvatarGlow.changes(),
		OptionAvatarOnlineColor.changes(),
		OptionAvatarOfflineColor.changes(),
		OptionCustomColor.changes(),
		OptionPalette.changes());
}

bool NightEventActive() {
	return OptionEffects.value()
		&& OptionNightEvents.value()
		&& (QTime::currentTime().hour() == 3);
}

AvatarGlow CurrentAvatarGlow() {
	auto result = AvatarGlow();
	result.enabled = OptionEffects.value() && OptionAvatarGlow.value();
	result.online = QColor::fromRgba(QRgb(uint(OptionAvatarOnlineColor.value())));
	const auto offline = uint(OptionAvatarOfflineColor.value());
	result.offline = offline
		? QColor::fromRgba(QRgb(offline))
		: PaletteAt(
			std::clamp(OptionPalette.value(), 0, PalettesCount() - 1),
			0.).first;
	return result;
}

CardStyle CurrentCardStyle() {
	auto result = CardStyle();
	result.enabled = OptionEffects.value() && OptionCards3d.value();
	result.alpha = ChatListAlpha();
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
		OptionPalette.changes(),
		OptionCustomColor.changes(),
		OptionChatListOpacity.changes(),
		OptionAvatarGlow.changes(),
		OptionAvatarOnlineColor.changes(),
		OptionAvatarOfflineColor.changes());
}

rpl::producer<> InterfaceConfigChanges() {
	return rpl::merge(
		OptionFlyby.changes(),
		OptionSeasons.changes(),
		OptionMusicPulse.changes(),
		OptionGpu.changes(),
		LaptopSavingChanges(),
		OptionEffects.changes(),
		OptionUiScene.changes(),
		OptionButtons3d.changes(),
		OptionButtonsAnimated.changes(),
		OptionCursorTrail.changes(),
		OptionSendEffect.changes(),
		OptionRoom.changes(),
		OptionPalette.changes());
}

bool OptionValue(const char id[]) {
	return base::options::value<bool>(id);
}

int OptionInt(const char id[]) {
	return base::options::value<int>(id);
}

rpl::producer<> OptionChanges(const char id[]) {
	return base::options::details::Lookup(id).changes();
}

QString OptionString(const char id[]) {
	return base::options::value<QString>(id);
}

} // namespace Mwtk
