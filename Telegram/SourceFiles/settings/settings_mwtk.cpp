/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_mwtk.h"

#include "base/options.h"
#include "chat_helpers/stickers_list_widget.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "dialogs/dialogs_entry.h"
#include "history/view/history_view_message.h"
#include "info/profile/info_profile_actions.h"
#include "lang/lang_keys.h"
#include "media/player/media_player_instance.h"
#include "mwtk/mwtk_options.h"
#include "mwtk/mwtk_overlay.h"
#include "mwtk/mwtk_deleted.h"
#include "mwtk/mwtk_floating.h"
#include "mwtk/mwtk_hidden.h"
#include "mwtk/mwtk_appicon.h"
#include "mwtk/mwtk_hotkeys.h"
#include "mwtk/mwtk_lyrics.h"
#include "mwtk/mwtk_notify.h"
#include "mwtk/mwtk_phrases.h"
#include "mwtk/mwtk_share.h"
#include "mwtk/mwtk_music.h"
#include "mwtk/mwtk_playlists.h"
#include "mwtk/mwtk_room.h"
#include "mwtk/mwtk_updater.h"
#include "base/timer.h"
#include "settings/settings_common.h"
#include "settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtGui/QLinearGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Settings {
namespace {

[[nodiscard]] QString Utf(const char *text) {
	return QString::fromUtf8(text);
}

void AddOptionToggle(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		const char id[]) {
	auto &option = base::options::lookup<bool>(id);
	const auto button = container->add(object_ptr<Button>(
		container,
		rpl::single(title),
		st::settingsButtonNoIcon));
	button->toggleOn(rpl::single(
		option.value()
	) | rpl::then(option.changes() | rpl::map([&option] {
		return option.value();
	})));
	button->toggledChanges(
	) | rpl::filter([&option](bool toggled) {
		return toggled != option.value();
	}) | rpl::on_next([=, &option](bool toggled) {
		option.set(toggled);
		if (option.restartRequired()) {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		}
	}, button->lifetime());
}

void AddPreview(not_null<Ui::VerticalLayout*> container) {
	const auto height = st::settingsButton.height * 4;
	const auto wrap = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	wrap->resize(wrap->width(), height);
	wrap->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(wrap);
		p.setRenderHint(QPainter::Antialiasing);
		auto gradient = QLinearGradient(0, 0, wrap->width(), height);
		gradient.setColorAt(0., QColor(14, 8, 26));
		gradient.setColorAt(1., QColor(28, 10, 34));
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		const auto radius = st::settingsButton.height / 3.;
		p.drawRoundedRect(wrap->rect(), radius, radius);
	}, wrap->lifetime());

	const auto preview = Ui::CreateChild<::Mwtk::VisualsWidget>(
		wrap,
		false);
	const auto refresh = [=] {
		auto config = ::Mwtk::CurrentConfig(style::Scale() / 100.);
		config.opacity = 1.;
		preview->setConfig(config);
		preview->setRunning(true);
	};
	::Mwtk::ConfigChanges(
	) | rpl::on_next(refresh, preview->lifetime());
	refresh();
	wrap->sizeValue(
	) | rpl::on_next([=](QSize size) {
		preview->setGeometry(QRect(QPoint(), size));
	}, preview->lifetime());
}

void ShowColorBox(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const char id[],
		QColor fallback,
		Fn<void()> done = nullptr) {
	auto &option = base::options::lookup<int>(id);
	const auto value = uint(option.value());
	const auto current = value ? QColor::fromRgba(QRgb(value)) : fallback;
	controller->show(Box([=, &option](not_null<Ui::GenericBox*> box) {
		const auto editor = box->addRow(object_ptr<ColorEditor>(
			box,
			ColorEditor::Mode::HSL,
			current));
		const auto save = [=, &option] {
			auto color = editor->color();
			color.setAlpha(255);
			option.set(int(uint(color.rgba())));
			if (done) {
				done();
			}
			box->closeBox();
		};
		editor->submitRequests(
		) | rpl::on_next(save, editor->lifetime());
		box->setFocusCallback([=] {
			editor->setInnerFocus();
		});
		box->addButton(rpl::single(Utf("Сохранить")), save);
		box->addButton(rpl::single(Utf("Отмена")), [=] { box->closeBox(); });
		box->setTitle(rpl::single(title));
		box->setWidth(editor->width());
	}));
}

void AddPalettePicker(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	auto &option = base::options::lookup<int>(::Mwtk::kOptionPalette);
	const auto count = ::Mwtk::PalettesCount();
	const auto size = st::settingsButton.height;
	const auto skip = size / 3;
	const auto picker = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	picker->resize(picker->width(), size + skip);
	picker->setCursor(style::cur_pointer);

	const auto layout = [=](int index) {
		const auto full = picker->width();
		const auto step = std::min(size + skip, full / std::max(count, 1));
		const auto left = (full - step * count) / 2;
		return QRect(left + index * step + (step - size) / 2, skip / 2, size, size);
	};
	picker->paintRequest(
	) | rpl::on_next([=, &option] {
		auto p = QPainter(picker);
		p.setRenderHint(QPainter::Antialiasing);
		const auto selected = option.value();
		for (auto i = 0; i != count; ++i) {
			const auto rect = QRectF(layout(i));
			const auto palette = ::Mwtk::PaletteAt(i, 0.);
			auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
			if (i == ::Mwtk::kCustomPaletteIndex - 1) {
				for (auto k = 0; k != 7; ++k) {
					gradient.setColorAt(k / 6., QColor::fromHsvF(k / 7.f, 0.85f, 1.f));
				}
			} else {
				gradient.setColorAt(0., palette.first);
				gradient.setColorAt(0.5, palette.second);
				gradient.setColorAt(1., palette.third);
			}
			const auto inner = rect.marginsRemoved({ 5., 5., 5., 5. });
			p.setPen(QPen(QColor(128, 128, 128, 120), 1.));
			p.setBrush(gradient);
			p.drawEllipse(inner);
			if (i == selected) {
				p.setBrush(Qt::NoBrush);
				p.setPen(QPen(st::windowActiveTextFg->c, 2.5));
				p.drawEllipse(rect.marginsRemoved({ 1.5, 1.5, 1.5, 1.5 }));
			}
		}
	}, picker->lifetime());
	option.changes(
	) | rpl::on_next([=] {
		picker->update();
	}, picker->lifetime());
	picker->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::MouseButtonPress;
	}) | rpl::on_next([=, &option](not_null<QEvent*> e) {
		const auto position = static_cast<QMouseEvent*>(e.get())->pos();
		for (auto i = 0; i != count; ++i) {
			if (layout(i).contains(position)) {
				if (i == ::Mwtk::kCustomPaletteIndex) {
					ShowColorBox(
						controller,
						Utf("Свой цвет"),
						::Mwtk::kOptionCustomColor,
						QColor(0, 229, 255),
						[=, &option] { option.set(i); });
				} else {
					option.set(i);
				}
				break;
			}
		}
	}, picker->lifetime());

	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(rpl::empty) | rpl::then(
				option.changes()
			) | rpl::map([&option] {
				return ::Mwtk::PaletteName(option.value());
			}),
			st::boxDividerLabel),
		st::boxRowPadding);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void AddSegmented(
		not_null<Ui::VerticalLayout*> container,
		const char id[],
		std::vector<QString> labels) {
	auto &option = base::options::lookup<int>(id);
	const auto count = int(labels.size());
	const auto height = st::settingsButton.height;
	const auto picker = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	picker->resize(picker->width(), height);
	picker->setCursor(style::cur_pointer);

	const auto segment = [=](int index) {
		const auto width = picker->width() / std::max(count, 1);
		return QRect(index * width, 0, width, height);
	};
	picker->paintRequest(
	) | rpl::on_next([=, &option] {
		auto p = QPainter(picker);
		p.setRenderHint(QPainter::Antialiasing);
		const auto radius = height / 3.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(picker->rect(), radius, radius);
		const auto selected = option.value();
		const auto palette = ::Mwtk::PaletteAt(
			base::options::value<int>(::Mwtk::kOptionPalette),
			0.);
		p.setFont(st::semiboldFont);
		for (auto i = 0; i != count; ++i) {
			const auto rect = segment(i);
			if (i == selected) {
				auto gradient = QLinearGradient(rect.topLeft(), rect.topRight());
				gradient.setColorAt(0., palette.first);
				gradient.setColorAt(1., palette.third);
				p.setPen(Qt::NoPen);
				p.setBrush(gradient);
				p.drawRoundedRect(
					QRectF(rect).marginsRemoved({ 3., 3., 3., 3. }),
					radius,
					radius);
				p.setPen(palette.first.lightnessF() > 0.7f
					? QColor(0, 0, 0)
					: QColor(255, 255, 255));
			} else {
				p.setPen(st::windowFg);
			}
			p.drawText(rect, Qt::AlignCenter, labels[i]);
		}
	}, picker->lifetime());
	rpl::merge(
		option.changes(),
		base::options::lookup<int>(::Mwtk::kOptionPalette).changes()
	) | rpl::on_next([=] {
		picker->update();
	}, picker->lifetime());
	picker->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::MouseButtonPress;
	}) | rpl::on_next([=, &option](not_null<QEvent*> e) {
		const auto position = static_cast<QMouseEvent*>(e.get())->pos();
		for (auto i = 0; i != count; ++i) {
			if (segment(i).contains(position)) {
				option.set(i);
				break;
			}
		}
	}, picker->lifetime());
}

// Tabs of the MWTK settings: «Вид», «Музыка», «Чаты», «Ещё».
[[nodiscard]] rpl::variable<int> &CurrentTab() {
	static auto result = rpl::variable<int>(0);
	return result;
}

void AddTabs(
		not_null<Ui::VerticalLayout*> container,
		std::vector<QString> labels) {
	const auto count = int(labels.size());
	const auto height = int(st::settingsButton.height * 1.15);
	const auto picker = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	picker->resize(picker->width(), height);
	picker->setCursor(style::cur_pointer);
	const auto segment = [=](int index) {
		const auto width = picker->width() / std::max(count, 1);
		return QRect(index * width, 0, width, height);
	};
	picker->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(picker);
		p.setRenderHint(QPainter::Antialiasing);
		const auto radius = height / 3.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(picker->rect(), radius, radius);
		const auto selected = CurrentTab().current();
		const auto palette = ::Mwtk::PaletteAt(
			base::options::value<int>(::Mwtk::kOptionPalette),
			0.);
		p.setFont(st::semiboldFont);
		for (auto i = 0; i != count; ++i) {
			const auto rect = segment(i);
			if (i == selected) {
				auto gradient = QLinearGradient(rect.topLeft(), rect.topRight());
				gradient.setColorAt(0., palette.first);
				gradient.setColorAt(1., palette.third);
				p.setPen(Qt::NoPen);
				p.setBrush(gradient);
				p.drawRoundedRect(
					QRectF(rect).marginsRemoved({ 3., 3., 3., 3. }),
					radius,
					radius);
				p.setPen(palette.first.lightnessF() > 0.7f
					? QColor(0, 0, 0)
					: QColor(255, 255, 255));
			} else {
				p.setPen(st::windowFg);
			}
			p.drawText(rect, Qt::AlignCenter, labels[i]);
		}
	}, picker->lifetime());
	CurrentTab().changes(
	) | rpl::on_next([=] {
		picker->update();
	}, picker->lifetime());
	picker->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::MouseButtonPress;
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto position = static_cast<QMouseEvent*>(e.get())->pos();
		for (auto i = 0; i != count; ++i) {
			if (segment(i).contains(position)) {
				CurrentTab() = i;
				break;
			}
		}
	}, picker->lifetime());
}

// A page of the tabs: shown only while its tab is selected.
[[nodiscard]] not_null<Ui::VerticalLayout*> AddTabPage(
		not_null<Ui::VerticalLayout*> container,
		int index) {
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	wrap->toggleOn(CurrentTab().value(
	) | rpl::map(rpl::mappers::_1 == index), anim::type::instant);
	return wrap->entity();
}

void AddPresetRow(not_null<Ui::VerticalLayout*> container) {
	const auto count = ::Mwtk::kPresetCount;
	const auto height = st::settingsButton.height;
	const auto row = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	row->resize(row->width(), height);
	row->setCursor(style::cur_pointer);
	struct State {
		int flash = -1;
		base::Timer timer;
	};
	const auto state = row->lifetime().make_state<State>();
	state->timer.setCallback([=] {
		state->flash = -1;
		row->update();
	});
	const auto segment = [=](int index) {
		const auto skip = height / 6;
		const auto width = (row->width() - skip * (count - 1)) / std::max(count, 1);
		return QRect(index * (width + skip), 0, width, height);
	};
	row->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(row);
		p.setRenderHint(QPainter::Antialiasing);
		const auto radius = height / 2.;
		const auto palette = ::Mwtk::PaletteAt(
			base::options::value<int>(::Mwtk::kOptionPalette),
			0.);
		p.setFont(st::semiboldFont);
		for (auto i = 0; i != count; ++i) {
			const auto rect = QRectF(segment(i)).marginsRemoved({ 1., 3., 1., 3. });
			if (i == state->flash) {
				auto gradient = QLinearGradient(rect.topLeft(), rect.topRight());
				gradient.setColorAt(0., palette.first);
				gradient.setColorAt(1., palette.third);
				p.setPen(Qt::NoPen);
				p.setBrush(gradient);
			} else {
				p.setPen(QPen(palette.first, 1.5));
				p.setBrush(st::windowBgOver);
			}
			p.drawRoundedRect(rect, radius, radius);
			p.setPen((i == state->flash)
				? ((palette.first.lightnessF() > 0.7f)
					? QColor(0, 0, 0)
					: QColor(255, 255, 255))
				: st::windowFg->c);
			p.drawText(rect, Qt::AlignCenter, ::Mwtk::PresetName(i));
		}
	}, row->lifetime());
	row->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::MouseButtonPress;
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto position = static_cast<QMouseEvent*>(e.get())->pos();
		for (auto i = 0; i != count; ++i) {
			if (segment(i).contains(position)) {
				::Mwtk::ApplyPreset(i);
				state->flash = i;
				state->timer.callOnce(900);
				row->update();
				break;
			}
		}
	}, row->lifetime());
}

void AddSlider(
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		const char id[],
		int min,
		int max,
		const QString &suffix) {
	auto &option = base::options::lookup<int>(id);
	const auto height = int(st::settingsButton.height * 1.5);
	const auto slider = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	slider->resize(slider->width(), height);
	slider->setCursor(style::cur_pointer);
	const auto dragging = slider->lifetime().make_state<bool>(false);

	const auto track = [=] {
		const auto knob = height / 5.;
		return QRectF(knob, height * 0.68, slider->width() - 2 * knob, 4.);
	};
	const auto apply = [=, &option](QPoint position) {
		const auto line = track();
		const auto ratio = std::clamp(
			(position.x() - line.left()) / std::max(line.width(), 1.),
			0.,
			1.);
		option.set(min + int(std::round(ratio * (max - min))));
	};
	slider->paintRequest(
	) | rpl::on_next([=, &option] {
		auto p = QPainter(slider);
		p.setRenderHint(QPainter::Antialiasing);
		const auto palette = ::Mwtk::PaletteAt(
			base::options::value<int>(::Mwtk::kOptionPalette),
			0.);
		const auto value = std::clamp(option.value(), min, max);
		const auto ratio = (value - min) / double(std::max(max - min, 1));
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(0, 0, slider->width(), int(height * 0.5)),
			Qt::AlignLeft | Qt::AlignVCenter,
			title);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(0, 0, slider->width(), int(height * 0.5)),
			Qt::AlignRight | Qt::AlignVCenter,
			QString::number(value) + suffix);
		const auto line = track();
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(line, 2., 2.);
		auto filled = line;
		filled.setWidth(line.width() * ratio);
		auto gradient = QLinearGradient(line.topLeft(), line.topRight());
		gradient.setColorAt(0., palette.first);
		gradient.setColorAt(1., palette.third);
		p.setBrush(gradient);
		p.drawRoundedRect(filled, 2., 2.);
		const auto knob = QPointF(filled.right(), line.center().y());
		const auto r = height / 7.;
		p.setBrush(QColor(0, 0, 0, 60));
		p.drawEllipse(knob + QPointF(0, 1.5), r, r);
		p.setBrush(palette.second);
		p.drawEllipse(knob, r, r);
		p.setBrush(QColor(255, 255, 255, 160));
		p.drawEllipse(knob - QPointF(r * 0.3, r * 0.3), r * 0.35, r * 0.35);
	}, slider->lifetime());
	rpl::merge(
		option.changes(),
		base::options::lookup<int>(::Mwtk::kOptionPalette).changes()
	) | rpl::on_next([=] {
		slider->update();
	}, slider->lifetime());
	slider->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			*dragging = true;
			apply(static_cast<QMouseEvent*>(e.get())->pos());
		} else if (type == QEvent::MouseMove && *dragging) {
			apply(static_cast<QMouseEvent*>(e.get())->pos());
		} else if (type == QEvent::MouseButtonRelease) {
			*dragging = false;
		}
	}, slider->lifetime());
}

void AddRoomPreview(not_null<Ui::VerticalLayout*> container) {
	const auto height = st::settingsButton.height * 4;
	const auto wrap = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	wrap->resize(wrap->width(), height);
	struct State {
		::Mwtk::RoomFrame frame;
		base::Timer timer;
	};
	const auto state = wrap->lifetime().make_state<State>();
	const auto render = [=] {
		auto params = ::Mwtk::CurrentRoomParams();
		params.scale = 1.;
		state->frame = ::Mwtk::RenderRoom(params, wrap->size());
		wrap->update();
	};
	state->timer.setCallback(render);
	rpl::merge(
		::Mwtk::RoomConfigChanges(),
		wrap->sizeValue() | rpl::to_empty
	) | rpl::on_next([=] {
		state->timer.callOnce(60);
	}, wrap->lifetime());
	::Mwtk::RoomRepaintRequests(
	) | rpl::on_next([=] {
		wrap->update();
	}, wrap->lifetime());
	wrap->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(wrap);
		p.setRenderHint(QPainter::Antialiasing);
		const auto radius = st::settingsButton.height / 3.;
		auto clip = QPainterPath();
		clip.addRoundedRect(QRectF(wrap->rect()), radius, radius);
		p.setClipPath(clip);
		if (state->frame.layers.empty() && !state->frame.video) {
			p.fillRect(wrap->rect(), st::windowBgOver);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				wrap->rect(),
				Qt::AlignCenter,
				Utf("Фон выключен"));
			return;
		}
		if (state->frame.video) {
			::Mwtk::PaintRoomVideo(p, wrap->size());
		}
		::Mwtk::PaintRoomFrame(
			p,
			state->frame,
			wrap->size(),
			::Mwtk::RoomCameraShift(),
			::Mwtk::RoomTime(),
			base::options::value<bool>(::Mwtk::kOptionRoomMotion));
	}, wrap->lifetime());
}

void ChooseBackgroundFile(QPointer<QWidget> parent, bool video) {
	const auto filter = video
		? (Utf("Видео (*.mp4 *.webm *.mov *.mkv *.avi *.gif);;")
			+ FileDialog::AllFilesFilter())
		: FileDialog::ImagesFilter();
	FileDialog::GetOpenPath(
		parent,
		video ? Utf("Выбери видео для фона") : Utf("Выбери фото для фона"),
		filter,
		[=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			const auto source = result.paths.front();
			const auto info = QFileInfo(source);
			const auto prefix = video ? u"video_"_q : u"photo_"_q;
			const auto folder = cWorkingDir() + u"tdata/mwtk_backgrounds/"_q;
			auto target = source;
			constexpr auto kMaxCopySize = qint64(300) * 1024 * 1024;
			if (info.size() < kMaxCopySize && QDir().mkpath(folder)) {
				const auto entries = QDir(folder).entryInfoList(QDir::Files);
				for (const auto &entry : entries) {
					if (entry.fileName().startsWith(prefix)) {
						QFile::remove(entry.absoluteFilePath());
					}
				}
				const auto copy = folder
					+ prefix
					+ QString::number(QDateTime::currentSecsSinceEpoch())
					+ '.'
					+ info.suffix().toLower();
				if (QFile::copy(source, copy)) {
					target = copy;
				}
			}
			base::options::lookup<QString>(video
				? ::Mwtk::kOptionBackgroundVideo
				: ::Mwtk::kOptionBackgroundPhoto).set(target);
			base::options::lookup<int>(::Mwtk::kOptionRoom).set(video ? 5 : 4);
		});
}

void EditSceneNotes(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		auto &option = base::options::lookup<QString>(
			::Mwtk::kOptionSceneNotes);
		box->setTitle(rpl::single(Utf("Тексты записок")));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			rpl::single(Utf("Каждая записка — с новой строки")),
			option.value()));
		field->setMaxLength(2000);
		field->setSubmitSettings(Ui::InputField::SubmitSettings::None);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Utf("Пусто — стандартные записки (HELP ME и другие).")),
			st::boxDividerLabel));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		box->addButton(rpl::single(Utf("Сохранить")), [=, &option] {
			option.set(field->getLastText().trimmed());
			box->closeBox();
		});
		box->addButton(rpl::single(Utf("Отмена")), [=] {
			box->closeBox();
		});
		box->addLeftButton(rpl::single(Utf("Сбросить")), [=, &option] {
			option.set(QString());
			box->closeBox();
		});
	}));
}

void AddActionButton(
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		Fn<void()> callback) {
	const auto button = container->add(object_ptr<Button>(
		container,
		rpl::single(title),
		st::settingsButtonNoIcon));
	button->setClickedCallback(std::move(callback));
}

[[nodiscard]] not_null<Ui::VerticalLayout*> AddRoomTypeSection(
		not_null<Ui::VerticalLayout*> container,
		Fn<bool(int)> shown) {
	auto &option = base::options::lookup<int>(::Mwtk::kOptionRoom);
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	wrap->toggleOn(rpl::single(
		option.value()
	) | rpl::then(option.changes() | rpl::map([&option] {
		return option.value();
	})) | rpl::map(shown));
	wrap->finishAnimating();
	return wrap->entity();
}

} // namespace

MwtkSettings::MwtkSettings(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MwtkSettings::title() {
	return rpl::single(u"MWTKgram"_q);
}

void MwtkSettings::setupContent() {
	const auto root = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto window = controller();

	Ui::AddSkip(root);
	AddPreview(root);
	Ui::AddSkip(root);

	AddOptionToggle(window, root, Utf("Все эффекты MWTKgram (F9)"), ::Mwtk::kOptionEffects);
	Ui::AddSkip(root);
	AddTabs(root, { Utf("Вид"), Utf("Музыка"), Utf("Чаты"), Utf("Ещё") });
	Ui::AddSkip(root);
	const auto designPage = AddTabPage(root, 0);
	const auto musicPage = AddTabPage(root, 1);
	const auto chatsPage = AddTabPage(root, 2);
	const auto otherPage = AddTabPage(root, 3);

	// «Вид»: everything about how the client looks.
	auto content = designPage;
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Пресеты в один клик")));
	AddPresetRow(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Качество графики")));
	AddSegmented(content, ::Mwtk::kOptionQuality, {
		::Mwtk::QualityName(0),
		::Mwtk::QualityName(1),
		::Mwtk::QualityName(2),
	});
	Ui::AddSkip(content);
	AddOptionToggle(window, content, Utf("Анимация, когда окно не активно"), ::Mwtk::kOptionBackgroundAnimation);
	AddOptionToggle(window, content, Utf("Рисовать эффекты на видеокарте"), ::Mwtk::kOptionGpu);
	AddOptionToggle(window, content, Utf("Режим ноутбука: от батареи — «Низкое»"), ::Mwtk::kOptionLaptopMode);
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::producer<QString>(rpl::single(Utf("Низкое — меньше всего нагрузки на процессор. "
			"«На видеокарте» переносит частицы, надписи и фигуры на GPU: процессор отдыхает. "
			"Если что-то мигает или пропадает — выключи. "
			"F9 мгновенно выключает и включает все эффекты."))));
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("Иконка приложения")));
	AddSegmented(content, ::Mwtk::kOptionAppIcon, {
		::Mwtk::AppIcon::Name(0),
		::Mwtk::AppIcon::Name(1),
		::Mwtk::AppIcon::Name(2),
		::Mwtk::AppIcon::Name(3),
	});
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Уведомления")));
	AddSegmented(content, ::Mwtk::kOptionNotifyStyle, {
		::Mwtk::Notify::StyleName(0),
		::Mwtk::Notify::StyleName(1),
		::Mwtk::Notify::StyleName(2),
		::Mwtk::Notify::StyleName(3),
	});
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::producer<QString>(rpl::single(Utf("Стиль видно у всплывающих окон MWTKgram. "
			"Если в настройках уведомлений включены системные уведомления Windows, "
			"их рисует сама система."))));
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("Визуалы")));
	AddOptionToggle(window, content, Utf("Включить визуалы"), ::Mwtk::kOptionVisuals);
	AddOptionToggle(window, content, Utf("Включить надпись doxmev"), ::Mwtk::kOptionWatermark);
	AddOptionToggle(window, content, Utf("3D-объекты"), ::Mwtk::kOptionObjects);
	AddOptionToggle(window, content, Utf("Светящиеся частицы"), ::Mwtk::kOptionParticles);
	AddOptionToggle(window, content, Utf("Времена года: снег, листья, гирлянда"), ::Mwtk::kOptionSeasons);
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("Цвет")));
	AddPalettePicker(window, content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Яркость")));
	AddSegmented(content, ::Mwtk::kOptionIntensity, {
		::Mwtk::IntensityName(0),
		::Mwtk::IntensityName(1),
		::Mwtk::IntensityName(2),
	});
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("Фон и 3D-сцены")));
	AddRoomPreview(content);
	Ui::AddSkip(content);
	{
		auto labels = std::vector<QString>();
		for (auto i = 0; i != ::Mwtk::kBackgroundCount; ++i) {
			labels.push_back(::Mwtk::BackgroundName(i));
		}
		AddSegmented(content, ::Mwtk::kOptionRoom, std::move(labels));
	}
	Ui::AddSkip(content);

	const auto custom = AddRoomTypeSection(content, [](int type) {
		return type == 3;
	});
	Ui::AddSubsectionTitle(custom, rpl::single(Utf("Конструктор сцены")));
	{
		auto bases = std::vector<QString>();
		for (auto i = 0; i != ::Mwtk::kSceneBaseCount; ++i) {
			bases.push_back(::Mwtk::SceneBaseName(i));
		}
		AddSegmented(custom, ::Mwtk::kOptionSceneBase, std::move(bases));
		Ui::AddSkip(custom);
		auto times = std::vector<QString>();
		for (auto i = 0; i != ::Mwtk::kSceneTimeCount; ++i) {
			times.push_back(::Mwtk::SceneTimeName(i));
		}
		AddSegmented(custom, ::Mwtk::kOptionSceneTime, std::move(times));
	}
	Ui::AddSkip(custom);
	AddSlider(custom, Utf("Густота: деревья, линии, звёзды"), ::Mwtk::kOptionSceneDensity, 0, 100, Utf("%"));
	AddSlider(custom, Utf("Туман и свечение"), ::Mwtk::kOptionSceneFog, 0, 100, Utf("%"));
	AddActionButton(custom, Utf("Тексты записок на деревьях"), [=] {
		EditSceneNotes(window);
	});
	Ui::AddSkip(custom);
	Ui::AddDividerText(
		custom,
		rpl::producer<QString>(rpl::single(Utf("Цвет неона и космоса берётся из цвета MWTKgram. "
			"Время суток меняет небо и туман в лесу."))));
	Ui::AddSkip(custom);

	const auto photo = AddRoomTypeSection(content, [](int type) {
		return type == 4;
	});
	AddActionButton(photo, Utf("Выбрать фото…"), [=] {
		ChooseBackgroundFile(this, false);
	});
	AddSlider(photo, Utf("Затемнение"), ::Mwtk::kOptionBackgroundDim, 0, 90, Utf("%"));
	Ui::AddSkip(photo);

	const auto video = AddRoomTypeSection(content, [](int type) {
		return type == 5;
	});
	AddActionButton(video, Utf("Выбрать видео…"), [=] {
		ChooseBackgroundFile(this, true);
	});
	AddSlider(video, Utf("Затемнение"), ::Mwtk::kOptionBackgroundDim, 0, 90, Utf("%"));
	Ui::AddSkip(video);
	Ui::AddDividerText(
		video,
		rpl::producer<QString>(rpl::single(Utf("Видео играет без звука по кругу и встаёт на паузу, "
			"когда окно не активно. Видео-фон нагружает процессор сильнее остальных."))));
	Ui::AddSkip(video);

	const auto scene = AddRoomTypeSection(content, [](int type) {
		return (type >= 1 && type <= 3);
	});
	AddSlider(scene, Utf("Поворот камеры"), ::Mwtk::kOptionRoomYaw, 0, 359, Utf("°"));
	AddSlider(scene, Utf("Наклон камеры"), ::Mwtk::kOptionRoomPitch, -15, 25, Utf("°"));
	AddSlider(scene, Utf("Угол обзора"), ::Mwtk::kOptionRoomFov, 45, 110, Utf("°"));
	AddOptionToggle(window, content, Utf("Фон следует за мышью"), ::Mwtk::kOptionRoomParallax);
	AddOptionToggle(window, content, Utf("Покачивание камеры и туман"), ::Mwtk::kOptionRoomMotion);
	AddOptionToggle(window, content, Utf("Секреты в 3 часа ночи"), ::Mwtk::kOptionNightEvents);
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::producer<QString>(rpl::single(Utf("Фон рисуется за сообщениями в чате. "
			"Своё фото или видео копируется в папку клиента, исходник можно удалить."))));
	Ui::AddSkip(content);

	// «Музыка»: player, lyrics, mix, karaoke, listening together.
	content = musicPage;
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Музыка")));
	AddActionButton(content, Utf("Мои плейлисты и «Мой микс»"), [=] {
		::Mwtk::Music::Show(window);
	});
	AddOptionToggle(window, content, Utf("Фон в такт музыке"), ::Mwtk::kOptionMusicPulse);
	AddOptionToggle(window, content, Utf("Приглашения «Слушать вместе»"), ::Mwtk::kOptionTogetherAllow);
	AddOptionToggle(window, content, Utf("Не включать следующий трек сам"), Media::Player::kOptionDisableAutoplayNext);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Текст песни")));
	AddOptionToggle(window, content, Utf("Летающий текст песни"), ::Mwtk::kOptionLyrics);
	AddOptionToggle(window, content, Utf("Искать текст в интернете (LRCLIB)"), ::Mwtk::kOptionLyricsOnline);
	AddOptionToggle(window, content, Utf("Спрятать чаты, пока играет текст"), ::Mwtk::kOptionHideChats);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Расположение слов")));
	AddSegmented(content, ::Mwtk::kOptionLyricsLayout, {
		::Mwtk::Lyrics::LayoutName(0),
		::Mwtk::Lyrics::LayoutName(1),
		::Mwtk::Lyrics::LayoutName(2),
	});
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Как пропадают слова")));
	AddSegmented(content, ::Mwtk::kOptionLyricsVanish, {
		::Mwtk::Lyrics::VanishName(0),
		::Mwtk::Lyrics::VanishName(1),
		::Mwtk::Lyrics::VanishName(2),
	});
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Стиль текста")));
	AddSegmented(content, ::Mwtk::kOptionLyricsStyle, {
		::Mwtk::Lyrics::StyleName(0),
		::Mwtk::Lyrics::StyleName(1),
		::Mwtk::Lyrics::StyleName(2),
	});
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Появление")));
	AddSegmented(content, ::Mwtk::kOptionLyricsAppear, {
		::Mwtk::Lyrics::AppearName(0),
		::Mwtk::Lyrics::AppearName(1),
		::Mwtk::Lyrics::AppearName(2),
		::Mwtk::Lyrics::AppearName(3),
		::Mwtk::Lyrics::AppearName(4),
	});
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Движение")));
	AddSegmented(content, ::Mwtk::kOptionLyricsDrift, {
		::Mwtk::Lyrics::DriftName(0),
		::Mwtk::Lyrics::DriftName(1),
		::Mwtk::Lyrics::DriftName(2),
		::Mwtk::Lyrics::DriftName(3),
	});
	Ui::AddSkip(content);
	AddOptionToggle(window, content, Utf("Текст дёргается в бит"), ::Mwtk::kOptionLyricsBeat);
	AddSlider(content, Utf("Движение слов"), ::Mwtk::kOptionLyricsMotion, 0, 100, Utf("%"));
	AddSlider(content, Utf("Размер текста"), ::Mwtk::kOptionLyricsSize, 60, 140, Utf("%"));
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::producer<QString>(rpl::single(Utf("Добавить трек: правый клик по музыке в чате → «В плейлист MWTK…». "
			"Плейлист играет треки по порядку или вперемешку, кнопки «вперёд/назад» в плеере идут по нему.\n\n"
			"Текст песни берётся из .lrc рядом с файлом или из базы LRCLIB. Если текста нет или он отстаёт — "
			"правый клик по треку → «Синхронизировать текст…» или «Сдвиг текста песни…».\n\n"
		"Своя обложка и караоке — правый клик по треку в чате. Слушать вместе — меню чата (⋯)."))));
	Ui::AddSkip(content);

	content = designPage;
	Ui::AddSubsectionTitle(content, rpl::single(Utf("3D-сообщения и чаты")));
	AddOptionToggle(window, content, Utf("Объёмные сообщения"), ::Mwtk::kOptionBubbles3d);
	AddOptionToggle(window, content, Utf("Сообщения в цвет темы"), ::Mwtk::kOptionBubblesTint);
	AddOptionToggle(window, content, Utf("Объёмные карточки чатов"), ::Mwtk::kOptionCards3d);
	AddOptionToggle(window, content, Utf("Плавное появление сообщений"), ::Mwtk::kOptionMessageAppear);
	AddSlider(content, Utf("Непрозрачность списка чатов (при фоне)"), ::Mwtk::kOptionChatListOpacity, 10, 100, Utf("%"));
	AddOptionToggle(window, content, Utf("Светящиеся аватарки"), ::Mwtk::kOptionAvatarGlow);
	AddOptionToggle(window, content, Utf("В сети: полоска на карточке"), ::Mwtk::kOptionOnlineStripe);
	AddOptionToggle(window, content, Utf("В сети: подсветка карточки"), ::Mwtk::kOptionOnlineCardGlow);
	AddOptionToggle(window, content, Utf("В сети: точка"), ::Mwtk::kOptionOnlineDot);
	AddOptionToggle(window, content, Utf("В сети: пульсирующая точка"), ::Mwtk::kOptionOnlinePulse);
	AddOptionToggle(window, content, Utf("В сети: аватарка-плитка"), ::Mwtk::kOptionOnlineTile);
	AddOptionToggle(window, content, Utf("Пролёт аватарки при новом сообщении"), ::Mwtk::kOptionFlyby);
	AddOptionToggle(window, content, Utf("Пролёт только для закреплённых чатов"), ::Mwtk::kOptionFlybyFavorites);
	AddOptionToggle(window, content, Utf("Голографический профиль"), ::Mwtk::kOptionHoloProfile);
	AddActionButton(content, Utf("Цвет свечения: в сети"), [=] {
		ShowColorBox(
			window,
			Utf("Цвет «в сети»"),
			::Mwtk::kOptionAvatarOnlineColor,
			QColor(57, 255, 136));
	});
	AddActionButton(content, Utf("Цвет свечения: не в сети"), [=] {
		ShowColorBox(
			window,
			Utf("Цвет «не в сети»"),
			::Mwtk::kOptionAvatarOfflineColor,
			::Mwtk::CurrentAvatarGlow().offline);
	});
	Ui::AddSkip(content);
	AddSegmented(content, ::Mwtk::kOptionBubblesOpacity, {
		::Mwtk::BubbleOpacityName(0),
		::Mwtk::BubbleOpacityName(1),
		::Mwtk::BubbleOpacityName(2),
	});
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("3D-интерфейс")));
	AddOptionToggle(window, content, Utf("Интерфейс внутри 3D-сцены"), ::Mwtk::kOptionUiScene);
	AddOptionToggle(window, content, Utf("Объёмные кнопки"), ::Mwtk::kOptionButtons3d);
	AddOptionToggle(window, content, Utf("Анимация кнопок"), ::Mwtk::kOptionButtonsAnimated);
	AddOptionToggle(window, content, Utf("Неоновый след за курсором"), ::Mwtk::kOptionCursorTrail);
	AddOptionToggle(window, content, Utf("Вспышка частиц при отправке"), ::Mwtk::kOptionSendEffect);
	AddOptionToggle(window, content, Utf("Звуки интерфейса"), ::Mwtk::kOptionUiSounds);
	AddSlider(content, Utf("Громкость звуков"), ::Mwtk::kOptionUiSoundsVolume, 1, 100, Utf("%"));
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	// «Ещё»: updates, sync, hotkeys and small switches.
	content = otherPage;
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Обновления и синхронизация")));
	AddActionButton(
		content,
		Utf("Проверить обновления (сборка ")
			+ QString::number(::Mwtk::Updater::CurrentBuild())
			+ ')',
		[=] { ::Mwtk::Updater::CheckNow(); });
	AddActionButton(content, Utf("Поделиться темой MWTK (файл)"), [=] {
		::Mwtk::Share::ExportTheme(window);
	});
	AddActionButton(content, Utf("Синхронизировать с телефоном (в «Избранное»)"), [=] {
		::Mwtk::Share::SendSync(window);
	});
	Ui::AddSkip(content);

	// «Чаты»: floating, hidden, deleted messages, phrases.
	content = chatsPage;
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Чаты")));
	AddActionButton(content, Utf("Летающие чаты (F8)"), [=] {
		::Mwtk::Floating::Toggle(window);
	});
	AddSlider(content, Utf("Сколько чатов летает"), ::Mwtk::kOptionFloatCount, 5, 40, QString());
	Ui::AddSkip(content);
	AddSegmented(content, ::Mwtk::kOptionFloatSource, {
		Utf("Последние"),
		Utf("Закреплённые"),
		Utf("Выбрать вручную"),
	});
	AddActionButton(content, Utf("Выбрать летающие чаты…"), [=] {
		::Mwtk::Floating::ShowPickBox(window);
	});
	AddActionButton(content, Utf("Скрытые чаты (Ctrl+Shift+L)"), [=] {
		::Mwtk::Hidden::Open(window);
	});
	AddOptionToggle(window, content, Utf("Сохранять удалённые сообщения"), ::Mwtk::kOptionSaveDeleted);
	AddOptionToggle(window, content, Utf("Оставлять удалённые прямо в чате"), ::Mwtk::kOptionKeepDeletedInChat);
	AddActionButton(content, Utf("Удалённые сообщения"), [=] {
		::Mwtk::Deleted::ShowBox(window, nullptr);
	});
	AddOptionToggle(window, content, Utf("Быстрые фразы через «/»"), ::Mwtk::kOptionQuickPhrases);
	AddActionButton(content, Utf("Мои быстрые фразы"), [=] {
		::Mwtk::Phrases::ShowEditBox(window);
	});
	Ui::AddSkip(content);

	content = otherPage;
	Ui::AddSubsectionTitle(content, rpl::single(Utf("Горячие клавиши")));
	for (auto i = 0; i != int(::Mwtk::Hotkeys::Action::Count); ++i) {
		const auto action = ::Mwtk::Hotkeys::Action(i);
		const auto button = content->add(object_ptr<Button>(
			content,
			rpl::single(::Mwtk::Hotkeys::ActionName(action)),
			st::settingsButtonNoIcon));
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			button,
			rpl::single(rpl::empty) | rpl::then(
				::Mwtk::Hotkeys::Changes()
			) | rpl::map([=] {
				const auto key = ::Mwtk::Hotkeys::Get(action);
				return key.isEmpty()
					? Utf("нет")
					: key.toString(QKeySequence::NativeText);
			}),
			st::defaultFlatLabel);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		rpl::combine(
			button->widthValue(),
			label->widthValue()
		) | rpl::on_next([=](int outer, int width) {
			label->moveToRight(
				st::settingsButtonNoIcon.padding.right(),
				(button->height() - label->height()) / 2,
				outer);
		}, label->lifetime());
		button->setClickedCallback([=] {
			::Mwtk::Hotkeys::ShowCaptureBox(window, action);
		});
	}
	AddOptionToggle(window, content, Utf("Показывать ID в профиле"), Info::Profile::kOptionShowPeerIdBelowAbout);
	AddOptionToggle(window, content, Utf("Дата вступления в канал в профиле"), Info::Profile::kOptionShowChannelJoinedBelowAbout);
	AddOptionToggle(window, content, Utf("«Открыть профиль» в меню чата"), ::Window::kOptionViewProfileInChatsListContextMenu);
	AddOptionToggle(window, content, Utf("Непрочитанные чаты сверху"), Dialogs::kOptionDialogsUnreadOnTop);
	AddOptionToggle(window, content, Utf("Безлимит недавних стикеров"), ChatHelpers::kOptionUnlimitedRecentStickers);
	AddOptionToggle(window, content, Utf("Сообщения на всю ширину"), HistoryView::kOptionUnlimitedMessageWidth);
	AddOptionToggle(window, content, Utf("Скрыть кнопку ИИ"), Ui::kOptionHideAiButton);
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::producer<QString>(rpl::single(Utf("MWTKgram основан на открытом коде Telegram Desktop. "
			"ID в профиле можно скопировать нажатием. Свои названия чатов, напоминания и заметки — "
			"в меню чата и сообщения (правый клик). F9 — эффекты, F8 — летающие чаты, "
			"Ctrl+Shift+L — скрытые чаты."))));

	Ui::ResizeFitChild(this, root);
}

} // namespace Settings
