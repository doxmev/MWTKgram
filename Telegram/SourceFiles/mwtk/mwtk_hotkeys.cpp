/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_hotkeys.h"

#include "base/event_filter.h"
#include "base/options.h"
#include "core/application.h"
#include "media/player/media_player_instance.h"
#include "mwtk/mwtk_lyrics.h"
#include "mwtk/mwtk_options.h"
#include "mwtk/mwtk_phrases.h"
#include "mwtk/mwtk_music.h"
#include "mwtk/mwtk_playlists.h"
#include "mwtk/mwtk_share.h"
#include "settings.h"
#include "settings/settings_mwtk.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>

#include <array>

namespace Mwtk::Hotkeys {
namespace {

constexpr auto kCount = int(Action::Count);

struct Storage {
	bool loaded = false;
	std::array<QKeySequence, kCount> keys;
	rpl::event_stream<> changes;
	bool capturing = false;
};

[[nodiscard]] Storage &State() {
	static auto result = Storage();
	return result;
}

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mwtk_hotkeys.json"_q;
}

[[nodiscard]] QString Id(Action action) {
	switch (action) {
	case Action::Playlists: return u"playlists"_q;
	case Action::HideChats: return u"hide_chats"_q;
	case Action::Settings: return u"settings"_q;
	case Action::NextTrack: return u"next_track"_q;
	case Action::PreviousTrack: return u"previous_track"_q;
	case Action::QualityCycle: return u"quality"_q;
	case Action::Phrases: return u"phrases"_q;
	case Action::Sync: return u"sync"_q;
	case Action::Count: break;
	}
	return QString();
}

[[nodiscard]] QKeySequence Default(Action action) {
	switch (action) {
	case Action::Playlists: return QKeySequence(u"Ctrl+Shift+P"_q);
	case Action::HideChats: return QKeySequence(u"Ctrl+Shift+H"_q);
	case Action::Settings: return QKeySequence(u"Ctrl+Shift+M"_q);
	case Action::NextTrack: return QKeySequence(u"Ctrl+Alt+Right"_q);
	case Action::PreviousTrack: return QKeySequence(u"Ctrl+Alt+Left"_q);
	default: break;
	}
	return QKeySequence();
}

void Load() {
	auto &state = State();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	for (auto i = 0; i != kCount; ++i) {
		state.keys[i] = Default(Action(i));
	}
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = 0; i != kCount; ++i) {
		const auto id = Id(Action(i));
		if (object.contains(id)) {
			state.keys[i] = QKeySequence(
				object.value(id).toString(),
				QKeySequence::PortableText);
		}
	}
}

void Save() {
	auto object = QJsonObject();
	const auto &state = State();
	for (auto i = 0; i != kCount; ++i) {
		object.insert(
			Id(Action(i)),
			state.keys[i].toString(QKeySequence::PortableText));
	}
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

[[nodiscard]] QKeySequence FromEvent(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	if (key == Qt::Key_unknown
		|| key == Qt::Key_Control
		|| key == Qt::Key_Shift
		|| key == Qt::Key_Alt
		|| key == Qt::Key_Meta) {
		return QKeySequence();
	}
	const auto modifiers = e->modifiers()
		& (Qt::ControlModifier
			| Qt::ShiftModifier
			| Qt::AltModifier
			| Qt::MetaModifier);
	return QKeySequence(QKeyCombination(modifiers, Qt::Key(key)));
}

[[nodiscard]] Window::SessionController *ActiveController() {
	const auto window = Core::App().activePrimaryWindow();
	return window ? window->sessionController() : nullptr;
}

void Run(Action action) {
	const auto controller = ActiveController();
	switch (action) {
	case Action::Playlists:
		if (controller) {
			Music::Show(controller);
		}
		break;
	case Action::HideChats:
		Lyrics::ToggleHideChats();
		break;
	case Action::Settings:
		if (controller) {
			controller->showSettings(::Settings::MwtkSettings::Id());
		}
		break;
	case Action::NextTrack:
		::Media::Player::instance()->next(AudioMsgId::Type::Song);
		break;
	case Action::PreviousTrack:
		::Media::Player::instance()->previous(AudioMsgId::Type::Song);
		break;
	case Action::QualityCycle: {
		auto &option = base::options::lookup<int>(kOptionQuality);
		option.set((option.value() + 1) % kQualityCount);
		if (controller) {
			controller->showToast(QString::fromUtf8("Качество MWTK: %1")
				.arg(QualityName(option.value())));
		}
	} break;
	case Action::Phrases:
		if (controller) {
			Phrases::ShowEditBox(controller);
		}
		break;
	case Action::Sync:
		if (controller) {
			Share::SendSync(controller);
		}
		break;
	case Action::Count:
		break;
	}
}

} // namespace

QString ActionName(Action action) {
	switch (action) {
	case Action::Playlists: return QString::fromUtf8("Плейлисты");
	case Action::HideChats: return QString::fromUtf8("Спрятать чаты (текст песни)");
	case Action::Settings: return QString::fromUtf8("Настройки MWTKgram");
	case Action::NextTrack: return QString::fromUtf8("Следующий трек");
	case Action::PreviousTrack: return QString::fromUtf8("Предыдущий трек");
	case Action::QualityCycle: return QString::fromUtf8("Сменить качество эффектов");
	case Action::Phrases: return QString::fromUtf8("Быстрые фразы");
	case Action::Sync: return QString::fromUtf8("Отправить синхронизацию");
	case Action::Count: break;
	}
	return QString();
}

QKeySequence Get(Action action) {
	Load();
	return State().keys[int(action)];
}

void Set(Action action, const QKeySequence &sequence) {
	Load();
	auto &state = State();
	// One combination does one thing.
	if (!sequence.isEmpty()) {
		for (auto &key : state.keys) {
			if (key == sequence) {
				key = QKeySequence();
			}
		}
	}
	state.keys[int(action)] = sequence;
	Save();
	state.changes.fire({});
}

rpl::producer<> Changes() {
	return State().changes.events();
}

void Setup() {
	static auto done = false;
	if (done) {
		return;
	}
	done = true;
	Load();
	static auto lifetime = rpl::lifetime();
	base::install_event_filter(
		QCoreApplication::instance(),
		[](not_null<QEvent*> e) {
			if (e->type() != QEvent::KeyPress || State().capturing) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(e.get());
			if (key->isAutoRepeat() || !EffectsEnabled()) {
				return base::EventFilterResult::Continue;
			}
			const auto sequence = FromEvent(key);
			if (sequence.isEmpty()) {
				return base::EventFilterResult::Continue;
			}
			const auto &keys = State().keys;
			for (auto i = 0; i != kCount; ++i) {
				if (!keys[i].isEmpty() && keys[i] == sequence) {
					const auto action = Action(i);
					crl::on_main([=] { Run(action); });
					return base::EventFilterResult::Cancel;
				}
			}
			return base::EventFilterResult::Continue;
		},
		lifetime);
}

void ShowCaptureBox(
		not_null<Window::SessionController*> controller,
		Action action) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(ActionName(action)));
		const auto current = Get(action);
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			QString::fromUtf8("Нажми новое сочетание клавиш…\nСейчас: %1").arg(
				current.isEmpty()
					? QString::fromUtf8("нет")
					: current.toString(QKeySequence::NativeText)),
			st::boxLabel));
		State().capturing = true;
		box->boxClosing() | rpl::on_next([] {
			State().capturing = false;
		}, box->lifetime());
		base::install_event_filter(box, QCoreApplication::instance(), [=](
				not_null<QEvent*> e) {
			if (e->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(e.get());
			if (key->key() == Qt::Key_Escape && !key->modifiers()) {
				return base::EventFilterResult::Continue;
			}
			const auto sequence = FromEvent(key);
			if (sequence.isEmpty()) {
				return base::EventFilterResult::Cancel;
			}
			Set(action, sequence);
			label->setText(QString::fromUtf8("Готово: %1").arg(
				sequence.toString(QKeySequence::NativeText)));
			crl::on_main(box, [=] { box->closeBox(); });
			return base::EventFilterResult::Cancel;
		});
		box->addButton(rpl::single(QString::fromUtf8("Убрать")), [=] {
			Set(action, QKeySequence());
			box->closeBox();
		});
		box->addButton(rpl::single(QString::fromUtf8("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Mwtk::Hotkeys
