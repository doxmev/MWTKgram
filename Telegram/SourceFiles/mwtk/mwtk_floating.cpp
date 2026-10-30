/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_floating.h"

#include "base/event_filter.h"
#include "base/options.h"
#include "base/timer.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "window/main_window.h"
#include "mwtk/mwtk_options.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/style/style_core_scale.h"
#include "ui/userpic_view.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>

#include <cmath>
#include <random>

namespace Mwtk::Floating {
namespace {

constexpr auto kTick = crl::time(16);

[[nodiscard]] int BubblesCount() {
	return std::clamp(OptionInt(kOptionFloatCount), 5, 40);
}

[[nodiscard]] std::vector<uint64> PickedIds() {
	auto result = std::vector<uint64>();
	for (const auto &part : OptionString(kOptionFloatPicked).split(
			',',
			Qt::SkipEmptyParts)) {
		auto ok = false;
		const auto value = part.trimmed().toULongLong(&ok);
		if (ok && value) {
			result.push_back(value);
		}
	}
	return result;
}

// The chats that fly: recent, pinned or picked by hand.
[[nodiscard]] std::vector<not_null<History*>> FloatingChats(
		not_null<Main::Session*> session) {
	const auto limit = size_t(BubblesCount());
	const auto source = std::clamp(OptionInt(kOptionFloatSource), 0, 2);
	auto result = std::vector<not_null<History*>>();
	if (source == 2) {
		for (const auto id : PickedIds()) {
			if (const auto peer = session->data().peerLoaded(PeerId(id))) {
				result.push_back(session->data().history(peer));
				if (result.size() >= limit) {
					break;
				}
			}
		}
		if (!result.empty()) {
			return result;
		}
	}
	const auto list = session->data().chatsList()->indexed();
	for (const auto &row : list->all()) {
		const auto history = row->history();
		if (!history) {
			continue;
		}
		if (source == 1 && !history->isPinnedDialog(FilterId())) {
			continue;
		}
		result.push_back(history);
		if (result.size() >= limit) {
			break;
		}
	}
	return result;
}

class FloatingWidget final : public Ui::RpWidget {
public:
	FloatingWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void startClosing();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	struct Bubble {
		not_null<PeerData*> peer;
		Ui::PeerUserpicView view;
		QString name;
		int unread = 0;
		QPointF position;
		QPointF velocity;
		double radius = 30.;
		double phase = 0.;
	};

	void tick();
	[[nodiscard]] int bubbleAt(QPointF point) const;

	const not_null<Window::SessionController*> _controller;
	std::vector<Bubble> _bubbles;
	base::Timer _timer;
	crl::time _last = 0;
	double _time = 0.;
	double _shown = 0.;
	bool _closing = false;

	int _dragging = -1;
	QPointF _dragOffset;
	QPointF _dragLast;
	crl::time _dragLastTime = 0;
	QPointF _dragVelocity;
	QPointF _pressPoint;
	bool _moved = false;

};

FloatingWidget::FloatingWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _timer([=] { tick(); }) {
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	const auto scale = style::Scale() / 100.;
	const auto random = std::make_shared<std::mt19937>(uint(crl::now()));
	const auto next = [=] {
		return std::uniform_real_distribution<double>(0., 1.)(*random);
	};
	const auto chats = FloatingChats(&controller->session());
	const auto many = (chats.size() > 20);
	for (const auto history : chats) {
		auto bubble = Bubble{ .peer = history->peer };
		bubble.name = history->peer->name();
		bubble.unread = history->unreadCount();
		bubble.radius = ((many ? 20. : 26.) + 10. * next()) * scale;
		bubble.phase = next() * 6.28;
		_bubbles.push_back(std::move(bubble));
	}
	sizeValue(
	) | rpl::on_next([=](QSize size) {
		for (auto &bubble : _bubbles) {
			if (bubble.position.isNull()) {
				bubble.position = QPointF(
					bubble.radius + next() * std::max(1., size.width() - 2 * bubble.radius),
					bubble.radius + next() * std::max(1., size.height() - 2 * bubble.radius));
				const auto angle = next() * 6.28;
				bubble.velocity = QPointF(std::cos(angle), std::sin(angle))
					* (40. + 60. * next()) * scale;
			}
		}
	}, lifetime());
	_last = crl::now();
	_timer.callEach(kTick);
}

void FloatingWidget::startClosing() {
	_closing = true;
	_dragging = -1;
}

int FloatingWidget::bubbleAt(QPointF point) const {
	for (auto i = int(_bubbles.size()) - 1; i >= 0; --i) {
		const auto &bubble = _bubbles[i];
		const auto d = point - bubble.position;
		if (d.x() * d.x() + d.y() * d.y() <= bubble.radius * bubble.radius) {
			return i;
		}
	}
	return -1;
}

void FloatingWidget::tick() {
	const auto now = crl::now();
	const auto dt = std::min((now - _last) / 1000., 0.05);
	_last = now;
	_time += dt;
	_shown += ((_closing ? 0. : 1.) - _shown) * std::min(1., dt * 8.);
	if (_closing && _shown < 0.02) {
		_timer.cancel();
		deleteLater();
		return;
	}
	const auto w = double(width());
	const auto h = double(height());
	const auto scale = style::Scale() / 100.;
	for (auto i = 0; i != int(_bubbles.size()); ++i) {
		auto &bubble = _bubbles[i];
		if (i == _dragging) {
			continue;
		}
		// Gentle drift so they keep floating.
		bubble.velocity += QPointF(
			std::sin(_time * 0.7 + bubble.phase),
			std::cos(_time * 0.6 + bubble.phase * 1.3)) * 14. * scale * dt;
		const auto speed = std::hypot(bubble.velocity.x(), bubble.velocity.y());
		const auto maxSpeed = 2600. * scale;
		if (speed > maxSpeed) {
			bubble.velocity *= maxSpeed / speed;
		}
		const auto slow = 30. * scale;
		if (speed > slow) {
			bubble.velocity *= std::exp(-dt * 0.9);
		}
		bubble.position += bubble.velocity * dt;
		const auto r = bubble.radius;
		if (bubble.position.x() < r) {
			bubble.position.setX(r);
			bubble.velocity.setX(std::abs(bubble.velocity.x()) * 0.8);
		} else if (bubble.position.x() > w - r) {
			bubble.position.setX(w - r);
			bubble.velocity.setX(-std::abs(bubble.velocity.x()) * 0.8);
		}
		if (bubble.position.y() < r) {
			bubble.position.setY(r);
			bubble.velocity.setY(std::abs(bubble.velocity.y()) * 0.8);
		} else if (bubble.position.y() > h - r) {
			bubble.position.setY(h - r);
			bubble.velocity.setY(-std::abs(bubble.velocity.y()) * 0.8);
		}
	}
	// Soft collisions.
	for (auto i = 0; i != int(_bubbles.size()); ++i) {
		for (auto j = i + 1; j != int(_bubbles.size()); ++j) {
			auto &a = _bubbles[i];
			auto &b = _bubbles[j];
			const auto d = b.position - a.position;
			const auto distance = std::hypot(d.x(), d.y());
			const auto minimal = a.radius + b.radius;
			if (distance <= 0.01 || distance >= minimal) {
				continue;
			}
			const auto n = d / distance;
			const auto overlap = minimal - distance;
			const auto aFixed = (i == _dragging);
			const auto bFixed = (j == _dragging);
			if (!aFixed) {
				a.position -= n * overlap * (bFixed ? 1. : 0.5);
			}
			if (!bFixed) {
				b.position += n * overlap * (aFixed ? 1. : 0.5);
			}
			const auto relative = (b.velocity - a.velocity).x() * n.x()
				+ (b.velocity - a.velocity).y() * n.y();
			if (relative < 0.) {
				const auto impulse = n * relative * 0.9;
				if (!aFixed) {
					a.velocity += impulse;
				}
				if (!bFixed) {
					b.velocity -= impulse;
				}
			}
		}
	}
	update();
}

void FloatingWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setOpacity(std::clamp(_shown, 0., 1.));
	p.fillRect(rect(), QColor(0, 0, 0, 150));
	const auto palette = PaletteAt(OptionInt(kOptionPalette), _time);
	const auto scale = style::Scale() / 100.;
	auto font = st::semiboldFont->f;
	p.setFont(font);
	for (auto i = 0; i != int(_bubbles.size()); ++i) {
		auto &bubble = _bubbles[i];
		const auto r = bubble.radius * (i == _dragging ? 1.12 : 1.);
		const auto center = bubble.position;
		auto glow = QRadialGradient(center, r * 1.6);
		auto glowColor = (i % 2) ? palette.first : palette.third;
		glowColor.setAlphaF(0.45f);
		glow.setColorAt(0.6, glowColor);
		glowColor.setAlphaF(0.f);
		glow.setColorAt(1., glowColor);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(center, r * 1.6, r * 1.6);
		const auto size = int(std::round(r * 2.));
		bubble.peer->paintUserpic(
			p,
			bubble.view,
			int(std::round(center.x() - r)),
			int(std::round(center.y() - r)),
			size,
			true);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(palette.first, 2. * scale));
		p.drawEllipse(center, r, r);
		const auto nameRect = QRectF(
			center.x() - r * 2.,
			center.y() + r + 4. * scale,
			r * 4.,
			st::semiboldFont->height + 2.);
		p.setPen(QColor(255, 255, 255));
		p.drawText(
			nameRect,
			Qt::AlignHCenter | Qt::AlignTop,
			st::semiboldFont->elided(bubble.name, int(r * 4.)));
		if (bubble.unread > 0) {
			const auto text = (bubble.unread > 99)
				? u"99+"_q
				: QString::number(bubble.unread);
			const auto bw = std::max(
				double(st::semiboldFont->width(text)) + 10. * scale,
				18. * scale);
			const auto badge = QRectF(
				center.x() + r * 0.45,
				center.y() - r - 2. * scale,
				bw,
				18. * scale);
			p.setPen(Qt::NoPen);
			p.setBrush(palette.first);
			p.drawRoundedRect(badge, badge.height() / 2., badge.height() / 2.);
			p.setPen(palette.first.lightnessF() > 0.7f
				? QColor(0, 0, 0)
				: QColor(255, 255, 255));
			p.drawText(badge, Qt::AlignCenter, text);
		}
	}
	p.setOpacity(std::clamp(_shown, 0., 1.) * 0.8);
	p.setPen(QColor(255, 255, 255));
	p.drawText(
		rect().marginsRemoved({ 0, 0, 0, int(14 * scale) }),
		Qt::AlignHCenter | Qt::AlignBottom,
		QString::fromUtf8("Хватай и кидай чаты · клик по чату — открыть · клик мимо — выйти"));
}

void FloatingWidget::mousePressEvent(QMouseEvent *e) {
	if (_closing) {
		return;
	}
	const auto point = e->position();
	_pressPoint = point;
	_moved = false;
	_dragging = bubbleAt(point);
	if (_dragging >= 0) {
		auto &bubble = _bubbles[_dragging];
		_dragOffset = bubble.position - point;
		_dragLast = point;
		_dragLastTime = crl::now();
		_dragVelocity = QPointF();
		bubble.velocity = QPointF();
		// Bring to front.
		std::rotate(
			_bubbles.begin() + _dragging,
			_bubbles.begin() + _dragging + 1,
			_bubbles.end());
		_dragging = int(_bubbles.size()) - 1;
	}
}

void FloatingWidget::mouseMoveEvent(QMouseEvent *e) {
	const auto point = e->position();
	if (_dragging < 0) {
		setCursor((bubbleAt(point) >= 0)
			? style::cur_pointer
			: style::cur_default);
		return;
	}
	const auto delta = point - _pressPoint;
	if (std::abs(delta.x()) + std::abs(delta.y()) > 6.) {
		_moved = true;
	}
	const auto now = crl::now();
	const auto dt = std::max((now - _dragLastTime) / 1000., 0.001);
	const auto instant = (point - _dragLast) / dt;
	_dragVelocity = _dragVelocity * 0.6 + instant * 0.4;
	_dragLast = point;
	_dragLastTime = now;
	_bubbles[_dragging].position = point + _dragOffset;
}

void FloatingWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_dragging >= 0) {
		auto &bubble = _bubbles[_dragging];
		if (!_moved) {
			const auto peer = bubble.peer;
			_dragging = -1;
			startClosing();
			_controller->showPeerHistory(peer);
			return;
		}
		const auto stale = (crl::now() - _dragLastTime) > 80;
		bubble.velocity = stale ? QPointF() : _dragVelocity;
		_dragging = -1;
	} else if (!_moved && bubbleAt(e->position()) < 0) {
		startClosing();
	}
}

void FloatingWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		startClosing();
		return;
	}
	RpWidget::keyPressEvent(e);
}

[[nodiscard]] QPointer<FloatingWidget> &Current() {
	static auto result = QPointer<FloatingWidget>();
	return result;
}

} // namespace

void ShowPickBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Какие чаты летают")));
		box->setWidth(st::boxWideWidth);
		box->setMaxHeight(st::boxMaxListHeight * 2);
		const auto picked = PickedIds();
		const auto checks = std::make_shared<
			std::vector<std::pair<uint64, Ui::Checkbox*>>>();
		const auto list = controller->session().data().chatsList()->indexed();
		auto added = 0;
		for (const auto &row : list->all()) {
			const auto history = row->history();
			if (!history) {
				continue;
			}
			const auto id = history->peer->id.value;
			const auto checked = ranges::contains(picked, id);
			const auto check = box->addRow(object_ptr<Ui::Checkbox>(
				box,
				history->peer->name(),
				checked,
				st::defaultBoxCheckbox));
			checks->emplace_back(id, check);
			if (++added >= 300) {
				break;
			}
		}
		box->addButton(rpl::single(QString::fromUtf8("Сохранить")), [=] {
			auto ids = QStringList();
			for (const auto &[id, check] : *checks) {
				if (check->checked()) {
					ids.push_back(QString::number(id));
				}
			}
			base::options::lookup<QString>(kOptionFloatPicked).set(
				ids.join(','));
			base::options::lookup<int>(kOptionFloatSource).set(2);
			box->closeBox();
		});
		box->addButton(rpl::single(QString::fromUtf8("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void Toggle(not_null<Window::SessionController*> controller) {
	auto &current = Current();
	if (current) {
		current->setAttribute(Qt::WA_TransparentForMouseEvents);
		current->startClosing();
		current = nullptr;
		return;
	}
	const auto parent = controller->widget()->bodyWidget();
	const auto widget = Ui::CreateChild<FloatingWidget>(parent, controller);
	widget->setGeometry(QRect(QPoint(), parent->size()));
	parent->sizeValue(
	) | rpl::on_next([=](QSize size) {
		widget->setGeometry(QRect(QPoint(), size));
	}, widget->lifetime());
	widget->show();
	widget->raise();
	widget->setFocus();
	current = widget;
}

void Setup() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	base::install_event_filter(
		QCoreApplication::instance(),
		[](not_null<QEvent*> e) {
			if (e->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(e.get());
			if (key->key() == Qt::Key_F8
				&& !key->isAutoRepeat()
				&& !(key->modifiers()
					& (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
				crl::on_main([] {
					const auto window = Core::App().activePrimaryWindow();
					if (const auto controller = window
							? window->sessionController()
							: nullptr) {
						Toggle(controller);
					}
				});
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});
}

} // namespace Mwtk::Floating
