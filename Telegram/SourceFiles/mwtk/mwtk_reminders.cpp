/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_reminders.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mwtk/mwtk_options.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QDateTime>

namespace Mwtk::Reminders {
namespace {

[[nodiscard]] TimeId Evening() {
	auto at = QDateTime::currentDateTime();
	auto target = QDateTime(at.date(), QTime(20, 0));
	if (at.secsTo(target) < 30 * 60) {
		target = target.addDays(1);
	}
	return TimeId(target.toSecsSinceEpoch());
}

[[nodiscard]] TimeId TomorrowMorning() {
	const auto tomorrow = QDate::currentDate().addDays(1);
	return TimeId(QDateTime(tomorrow, QTime(10, 0)).toSecsSinceEpoch());
}

void Schedule(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		TimeId when,
		const QString &label) {
	const auto session = &controller->session();
	const auto item = session->data().message(itemId);
	if (!item) {
		return;
	}
	const auto saved = session->data().history(session->userPeerId());
	auto options = Api::SendOptions();
	options.scheduled = std::max(when, base::unixtime::now() + 60);
	auto action = Api::SendAction(saved, options);
	action.clearDraft = false;
	action.generateLocal = false;
	session->api().forwardMessages(
		Data::ResolvedForwardDraft{ .items = { item } },
		action,
		[=] {
			controller->showToast(QString::fromUtf8(
				"Напомню %1 — придёт в «Избранное» на всех устройствах."
			).arg(label));
		});
}

} // namespace

void FillMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	if (!EffectsEnabled()) {
		return;
	}
	const auto item = controller->session().data().message(itemId);
	if (!item || !item->allowsForward() || !item->isRegular()) {
		return;
	}
	menu->addAction(QString::fromUtf8("Напомнить (MWTK)…"), [=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(QString::fromUtf8("Напомнить")));
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(QString::fromUtf8(
					"Сообщение придёт напоминанием в «Избранное»: "
					"и на компьютере, и на телефоне.")),
				st::boxDividerLabel));
			const auto add = [&](const QString &text, Fn<TimeId()> when) {
				box->addButton(rpl::single(text), [=] {
					Schedule(controller, itemId, when(), text.toLower());
					box->closeBox();
				});
			};
			add(QString::fromUtf8("Через час"), [] {
				return base::unixtime::now() + 3600;
			});
			add(QString::fromUtf8("Вечером"), [] { return Evening(); });
			add(QString::fromUtf8("Завтра"), [] { return TomorrowMorning(); });
		}));
	}, &st::menuIconSchedule);
}

} // namespace Mwtk::Reminders
