/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_phrases.h"

#include "base/unique_qptr.h"
#include "mwtk/mwtk_options.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>

namespace Mwtk::Phrases {
namespace {

struct Storage {
	bool loaded = false;
	QStringList list;
};

[[nodiscard]] Storage &State() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mwtk_phrases.json"_q;
}

void Load() {
	auto &state = State();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
		const auto text = value.toString().trimmed();
		if (!text.isEmpty()) {
			state.list.push_back(text);
		}
	}
}

} // namespace

QStringList List() {
	Load();
	return State().list;
}

void SetList(const QStringList &list) {
	Load();
	auto &state = State();
	state.list.clear();
	auto array = QJsonArray();
	for (const auto &item : list) {
		const auto text = item.trimmed();
		if (!text.isEmpty()) {
			state.list.push_back(text);
			array.push_back(text);
		}
	}
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void Attach(
		not_null<Ui::InputField*> field,
		not_null<Window::SessionController*> controller) {
	const auto menu = field->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	field->changes(
	) | rpl::on_next([=] {
		if (!EffectsEnabled() || !OptionValue(kOptionQuickPhrases)) {
			return;
		}
		if (field->getLastText() != u"/"_q || *menu) {
			return;
		}
		const auto phrases = List();
		*menu = base::make_unique_q<Ui::PopupMenu>(
			field,
			st::popupMenuWithIcons);
		for (const auto &phrase : phrases) {
			auto title = phrase;
			title.replace('\n', ' ');
			if (title.size() > 48) {
				title = title.left(46) + QString::fromUtf8("…");
			}
			(*menu)->addAction(title, [=] {
				field->setText(phrase);
				field->setCursorPosition(phrase.size());
				field->setFocus();
			});
		}
		if (!phrases.isEmpty()) {
			(*menu)->addSeparator();
		}
		(*menu)->addAction(
			QString::fromUtf8("Мои фразы…"),
			[=] { ShowEditBox(controller); },
			&st::menuIconEdit);
		(*menu)->setDestroyedCallback([=] {
			crl::on_main(field, [=] { *menu = nullptr; });
		});
		const auto global = field->mapToGlobal(QPoint(0, 0));
		(*menu)->popup(global - QPoint(0, (*menu)->height()));
	}, field->lifetime());
}

void ShowEditBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Быстрые фразы")));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8(
				"Каждая фраза с новой строки. В чате набери «/» — и выбери нужную.")),
			st::boxDividerLabel));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			rpl::single(QString::fromUtf8("скоро буду\nперезвоню позже")),
			List().join('\n')));
		field->setMaxLength(8000);
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		box->addButton(rpl::single(QString::fromUtf8("Сохранить")), [=] {
			SetList(field->getLastText().split('\n'));
			box->closeBox();
		});
		box->addButton(rpl::single(QString::fromUtf8("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Mwtk::Phrases
