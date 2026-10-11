/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Mwtk::Sounds {

enum class Sound {
	Click,
	Send,
	Toggle,
};

void Setup();
void Play(Sound sound);

} // namespace Mwtk::Sounds
