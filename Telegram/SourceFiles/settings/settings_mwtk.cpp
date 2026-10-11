/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_mwtk.h"

#include "base/options.h"
#include "chat_helpers/stickers_list_widget.h"
#include "core/application.h"
#include "dialogs/dialogs_entry.h"
#include "history/view/history_view_message.h"
#include "info/profile/info_profile_actions.h"
#include "lang/lang_keys.h"
#include "media/player/media_player_instance.h"
#include "mwtk/mwtk_options.h"
#include "mwtk/mwtk_overlay.h"
#include "mwtk/mwtk_room.h"
#include "base/timer.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

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

void AddPalettePicker(not_null<Ui::VerticalLayout*> container) {
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
			if (i == ::Mwtk::PalettesCount() - 1) {
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
				option.set(i);
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
		auto params = ::Mwtk::RoomParams();
		params.type = ::Mwtk::CurrentRoom();
		params.yaw = base::options::value<int>(::Mwtk::kOptionRoomYaw);
		params.pitch = base::options::value<int>(::Mwtk::kOptionRoomPitch);
		params.fov = base::options::value<int>(::Mwtk::kOptionRoomFov);
		params.ratio = style::DevicePixelRatio();
		params.accent = ::Mwtk::PaletteAt(
			base::options::value<int>(::Mwtk::kOptionPalette),
			0.).first;
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
		if (state->frame.layers.empty()) {
			p.fillRect(wrap->rect(), st::windowBgOver);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				wrap->rect(),
				Qt::AlignCenter,
				Utf("3D-комната выключена"));
			return;
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
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto window = controller();

	Ui::AddSkip(content);
	AddPreview(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("Визуалы")));
	AddOptionToggle(window, content, Utf("Включить визуалы"), ::Mwtk::kOptionVisuals);
	AddOptionToggle(window, content, Utf("Включить надпись doxmev"), ::Mwtk::kOptionWatermark);
	AddOptionToggle(window, content, Utf("3D-объекты"), ::Mwtk::kOptionObjects);
	AddOptionToggle(window, content, Utf("Светящиеся частицы"), ::Mwtk::kOptionParticles);
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("Цвет")));
	AddPalettePicker(content);
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

	Ui::AddSubsectionTitle(content, rpl::single(Utf("3D-комнаты")));
	AddRoomPreview(content);
	Ui::AddSkip(content);
	AddSegmented(content, ::Mwtk::kOptionRoom, {
		::Mwtk::RoomName(::Mwtk::RoomType::None),
		::Mwtk::RoomName(::Mwtk::RoomType::Forest),
		Utf("Неон"),
	});
	Ui::AddSkip(content);
	AddSlider(content, Utf("Поворот камеры"), ::Mwtk::kOptionRoomYaw, 0, 359, Utf("°"));
	AddSlider(content, Utf("Наклон камеры"), ::Mwtk::kOptionRoomPitch, -15, 25, Utf("°"));
	AddSlider(content, Utf("Угол обзора"), ::Mwtk::kOptionRoomFov, 45, 110, Utf("°"));
	AddOptionToggle(window, content, Utf("Камера следует за мышью"), ::Mwtk::kOptionRoomParallax);
	AddOptionToggle(window, content, Utf("Покачивание камеры и туман"), ::Mwtk::kOptionRoomMotion);
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::producer<QString>(rpl::single(Utf("3D-комната рисуется за сообщениями в чате. "
			"На слабом компьютере выключи покачивание или саму комнату."))));
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("3D-интерфейс")));
	AddOptionToggle(window, content, Utf("Интерфейс внутри 3D-сцены"), ::Mwtk::kOptionUiScene);
	AddOptionToggle(window, content, Utf("Объёмные кнопки"), ::Mwtk::kOptionButtons3d);
	AddOptionToggle(window, content, Utf("Анимация кнопок"), ::Mwtk::kOptionButtonsAnimated);
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, rpl::single(Utf("Полезные функции")));
	AddOptionToggle(window, content, Utf("Показывать ID в профиле"), Info::Profile::kOptionShowPeerIdBelowAbout);
	AddOptionToggle(window, content, Utf("Дата вступления в канал в профиле"), Info::Profile::kOptionShowChannelJoinedBelowAbout);
	AddOptionToggle(window, content, Utf("«Открыть профиль» в меню чата"), ::Window::kOptionViewProfileInChatsListContextMenu);
	AddOptionToggle(window, content, Utf("Непрочитанные чаты сверху"), Dialogs::kOptionDialogsUnreadOnTop);
	AddOptionToggle(window, content, Utf("Безлимит недавних стикеров"), ChatHelpers::kOptionUnlimitedRecentStickers);
	AddOptionToggle(window, content, Utf("Сообщения на всю ширину"), HistoryView::kOptionUnlimitedMessageWidth);
	AddOptionToggle(window, content, Utf("Не включать следующий трек сам"), Media::Player::kOptionDisableAutoplayNext);
	AddOptionToggle(window, content, Utf("Скрыть кнопку ИИ"), Ui::kOptionHideAiButton);
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		rpl::producer<QString>(rpl::single(Utf("MWTKgram основан на открытом коде Telegram Desktop. "
			"ID в профиле можно скопировать нажатием."))));

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
