/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_appicon.h"

#include "core/application.h"
#include "mwtk/mwtk_options.h"
#include "tray.h"
#include "window/main_window.h"
#include "window/window_controller.h"

namespace Mwtk::AppIcon {

QString Name(int index) {
	switch (index) {
	case 1: return QString::fromUtf8("Кровавый куб");
	case 2: return QString::fromUtf8("Глаз MWTK");
	case 3: return QString::fromUtf8("MWTKgram неон");
	}
	return QString::fromUtf8("Классическая M");
}

void Setup() {
	static auto done = false;
	if (done) {
		return;
	}
	done = true;
	static auto lifetime = rpl::lifetime();
	OptionChanges(
		kOptionAppIcon
	) | rpl::on_next([] {
		auto &app = Core::App();
		app.refreshApplicationIcon();
		app.tray().updateIconCounters();
		app.enumerateWindows([](not_null<Window::Controller*> window) {
			window->widget()->updateWindowIcon();
		});
	}, lifetime);
}

} // namespace Mwtk::AppIcon
