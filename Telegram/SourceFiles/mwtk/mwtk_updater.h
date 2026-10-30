/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Mwtk::Updater {

// Checks GitHub Releases for a newer installer a bit after the start.
void Setup();

// «Проверить обновления»: tells the result even when there is nothing new.
void CheckNow();

[[nodiscard]] int CurrentBuild();

} // namespace Mwtk::Updater
