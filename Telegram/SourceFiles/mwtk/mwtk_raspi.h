/*
This file is part of RASPIgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class InputField;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Raspi {

// Starts the daily weather check. Safe to call more than once.
void Setup();

// Adds «AI-редактор…» and «Чеклисты…» to the context menu of the
// message field; their results can go straight into the field.
void AttachToField(
	not_null<Ui::InputField*> field,
	not_null<Window::SessionController*> controller);

// Local checklists, kept only on this computer.
void ShowChecklists(
	not_null<Window::SessionController*> controller,
	Fn<void(QString)> insert = nullptr);

// Writes posts, headlines and pictures with the user's own API key.
void ShowAiEditor(
	not_null<Window::SessionController*> controller,
	QString initial = QString(),
	Fn<void(QString)> insert = nullptr);
void ShowAiSettings(not_null<Window::SessionController*> controller);
[[nodiscard]] QString AiProviderName(int index);

// Daily weather into «Избранное» as a reminder.
void ShowWeatherNow(not_null<Window::SessionController*> controller);
void EditWeatherCity(not_null<Window::SessionController*> controller);
void EditWeatherTime(not_null<Window::SessionController*> controller);
[[nodiscard]] QString WeatherTimeText();

// One-line text option editor (flying text and the like).
void EditTextOption(
	not_null<Window::SessionController*> controller,
	const QString &title,
	const QString &placeholder,
	const char id[],
	int maxLength);

} // namespace Mwtk::Raspi
