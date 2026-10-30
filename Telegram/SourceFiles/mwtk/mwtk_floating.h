/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Mwtk::Floating {

// Shows or hides the floating chats over the window (F8).
void Toggle(not_null<Window::SessionController*> controller);

// Lets the user tick the chats that fly (source «Выбрать вручную»).
void ShowPickBox(not_null<Window::SessionController*> controller);

// Installs the F8 hotkey.
void Setup();

} // namespace Mwtk::Floating
