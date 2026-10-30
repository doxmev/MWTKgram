/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_sounds.h"

#include "base/event_filter.h"
#include "media/audio/media_audio_track.h"
#include "mwtk/mwtk_options.h"
#include "ui/abstract_button.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <array>
#include <cmath>
#include <memory>

namespace Mwtk::Sounds {
namespace {

constexpr auto kRate = 44100;
constexpr auto kPi = 3.14159265358979323846;

void PutLe(bytes::vector &out, uint32 value, int size) {
	for (auto i = 0; i != size; ++i) {
		out.push_back(bytes::type((value >> (8 * i)) & 0xFF));
	}
}

void PutTag(bytes::vector &out, const char *tag) {
	for (auto i = 0; i != 4; ++i) {
		out.push_back(bytes::type(tag[i]));
	}
}

[[nodiscard]] bytes::vector MakeWav(const std::vector<float> &samples) {
	auto result = bytes::vector();
	const auto data = uint32(samples.size() * 2);
	result.reserve(44 + data);
	PutTag(result, "RIFF");
	PutLe(result, 36 + data, 4);
	PutTag(result, "WAVE");
	PutTag(result, "fmt ");
	PutLe(result, 16, 4);
	PutLe(result, 1, 2); // PCM
	PutLe(result, 1, 2); // Mono
	PutLe(result, kRate, 4);
	PutLe(result, kRate * 2, 4);
	PutLe(result, 2, 2);
	PutLe(result, 16, 2);
	PutTag(result, "data");
	PutLe(result, data, 4);
	for (const auto sample : samples) {
		const auto clamped = std::clamp(sample, -1.f, 1.f);
		PutLe(result, uint32(uint16(int16(clamped * 32000))), 2);
	}
	return result;
}

[[nodiscard]] std::vector<float> Synth(Sound sound) {
	auto result = std::vector<float>();
	const auto push = [&](double seconds, auto &&generator) {
		const auto count = int(seconds * kRate);
		for (auto i = 0; i != count; ++i) {
			const auto t = double(i) / kRate;
			result.push_back(float(generator(t, seconds)));
		}
	};
	switch (sound) {
	case Sound::Click:
		push(0.06, [](double t, double) {
			const auto envelope = std::exp(-t / 0.006);
			return 0.55 * envelope * (std::sin(2. * kPi * 2100. * t)
				+ 0.4 * std::sin(2. * kPi * 3300. * t));
		});
		break;
	case Sound::Send:
		push(0.22, [](double t, double length) {
			const auto attack = std::min(1., t / 0.008);
			const auto envelope = attack * std::exp(-t / 0.07);
			const auto progress = t / length;
			const auto phase = 2. * kPi * (520. * t + 350. * t * progress);
			return 0.5 * envelope * (std::sin(phase)
				+ 0.25 * std::sin(2. * phase));
		});
		break;
	case Sound::Toggle:
		push(0.07, [](double t, double) {
			return 0.4 * std::exp(-t / 0.03) * std::sin(2. * kPi * 660. * t);
		});
		push(0.11, [](double t, double) {
			return 0.4 * std::exp(-t / 0.045) * std::sin(2. * kPi * 990. * t);
		});
		break;
	}
	// Short fade out to avoid a click at the end.
	const auto fade = std::min(int(result.size()), kRate / 200);
	for (auto i = 0; i != fade; ++i) {
		result[result.size() - 1 - i] *= float(i) / fade;
	}
	return result;
}

struct State {
	std::array<std::unique_ptr<::Media::Audio::Track>, 3> tracks;
	crl::time lastClick = 0;
	bool setup = false;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] bool Enabled() {
	return EffectsEnabled() && OptionValue(kOptionUiSounds);
}

[[nodiscard]] bool IsButton(QObject *object) {
	auto widget = qobject_cast<QWidget*>(object);
	for (auto i = 0; widget && i != 3; ++i) {
		if (dynamic_cast<Ui::AbstractButton*>(widget)) {
			return true;
		}
		widget = widget->parentWidget();
	}
	return false;
}

} // namespace

void Play(Sound sound) {
	if (!Enabled()) {
		return;
	}
	auto &state = Instance();
	auto &track = state.tracks[int(sound)];
	if (!track) {
		track = ::Media::Audio::Current().createTrack();
		track->fillFromData(MakeWav(Synth(sound)));
	}
	if (track->failed()) {
		return;
	}
	const auto volume = std::clamp(OptionInt(kOptionUiSoundsVolume), 1, 100);
	track->playOnce(volume / 100.);
}

void Setup() {
	auto &state = Instance();
	if (state.setup) {
		return;
	}
	state.setup = true;
	base::install_event_filter(
		QCoreApplication::instance(),
		[](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseButtonRelease) {
				const auto mouse = static_cast<QMouseEvent*>(e.get());
				if (mouse->button() == Qt::LeftButton && Enabled()) {
					auto &state = Instance();
					const auto now = crl::now();
					if (now - state.lastClick > 80) {
						const auto receiver = QApplication::widgetAt(
							mouse->globalPosition().toPoint());
						if (IsButton(receiver)) {
							state.lastClick = now;
							crl::on_main([] { Play(Sound::Click); });
						}
					}
				}
			}
			return base::EventFilterResult::Continue;
		});
}

} // namespace Mwtk::Sounds
