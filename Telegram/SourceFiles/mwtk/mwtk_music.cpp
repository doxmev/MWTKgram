/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_music.h"

#include "apiwrap.h"
#include "data/data_audio_msg_id.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_messages.h"
#include "data/data_peer_id.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "mwtk/mwtk_lyrics.h"
#include "mwtk/mwtk_playlists.h"
#include "storage/storage_shared_media.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <map>

namespace Mwtk::Music {
namespace {

[[nodiscard]] QString Utf(const char *text) {
	return QString::fromUtf8(text);
}

[[nodiscard]] QColor Accent(int alpha = 255) {
	return QColor(255, 45, 70, alpha);
}

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] DocumentData *SongOf(HistoryItem *item) {
	if (!item) {
		return nullptr;
	}
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			if (document->isAudioFile()) {
				return document;
			}
		}
	}
	return nullptr;
}

[[nodiscard]] QImage Rounded(const QImage &source, int size, int radius) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(0., 0., size, size), radius, radius);
	p.setClipPath(path);
	const auto side = std::min(source.width(), source.height());
	p.drawImage(
		QRectF(0., 0., size, size),
		source,
		QRectF(
			(source.width() - side) / 2.,
			(source.height() - side) / 2.,
			side,
			side));
	p.end();
	return result;
}

void PaintPlaceholder(
		QPainter &p,
		QRectF rect,
		const QString &seed,
		double radius) {
	const auto hue = int(qHash(seed) % 360u);
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., QColor::fromHsv(hue, 190, 215));
	gradient.setColorAt(1., QColor::fromHsv((hue + 40) % 360, 180, 60));
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.fillPath(path, gradient);
	p.setPen(QColor(255, 255, 255, 200));
	auto font = st::semiboldFont->f;
	font.setPixelSize(std::max(int(rect.height() * 0.42), 8));
	p.setFont(font);
	p.drawText(rect, Qt::AlignCenter, Utf("♪"));
}

// Song covers (the thumbnail of the audio file) prepared once per size.
class Covers final {
public:
	explicit Covers(not_null<Main::Session*> session) : _session(session) {
	}

	[[nodiscard]] QImage get(FullMsgId id, int size, int radius) {
		if (!id.msg) {
			return QImage();
		}
		auto &entry = _entries[id];
		if (!entry.prepared.isNull() && entry.size == size) {
			return entry.prepared;
		}
		const auto document = SongOf(_session->data().message(id));
		if (!document || !document->hasThumbnail()) {
			return QImage();
		}
		if (!entry.media) {
			entry.media = document->createMediaView();
			entry.media->thumbnailWanted(Data::FileOrigin(id));
		}
		if (const auto image = entry.media->thumbnail()) {
			entry.prepared = Rounded(image->original(), size, radius);
			entry.size = size;
			return entry.prepared;
		}
		return QImage();
	}

	// The message may not be loaded yet (a playlist from another chat).
	void request(FullMsgId id, Fn<void()> done) {
		if (!id.msg || _session->data().message(id)) {
			return;
		}
		auto &entry = _entries[id];
		if (entry.requested) {
			return;
		}
		entry.requested = true;
		_session->api().requestMessageData(
			_session->data().peer(id.peer),
			id.msg,
			std::move(done));
	}

private:
	struct Entry {
		std::shared_ptr<Data::DocumentMedia> media;
		QImage prepared;
		int size = 0;
		bool requested = false;
	};

	const not_null<Main::Session*> _session;
	std::map<FullMsgId, Entry> _entries;

};

struct MusicState {
	explicit MusicState(not_null<Window::SessionController*> controller)
	: controller(controller)
	, session(&controller->session())
	, covers(&controller->session()) {
	}

	const not_null<Window::SessionController*> controller;
	const not_null<Main::Session*> session;
	Covers covers;
};

[[nodiscard]] FullMsgId TrackId(const Playlists::Track &track) {
	return FullMsgId(DeserializePeerId(track.peer), MsgId(track.msg));
}

[[nodiscard]] bool IsPlaying(FullMsgId id) {
	const auto current = ::Media::Player::instance()->current(
		AudioMsgId::Type::Song);
	return id.msg && current.audio() && (current.contextId() == id);
}

void PaintCollage(
		QPainter &p,
		QRect rect,
		const std::shared_ptr<MusicState> &state,
		int index,
		int radius) {
	const auto &list = Playlists::List(state->session);
	if (index < 0 || index >= int(list.size())) {
		return;
	}
	const auto &tracks = list[index].tracks;
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(rect), radius, radius);
	p.save();
	p.setClipPath(clip);
	const auto half = rect.width() / 2;
	for (auto i = 0; i != 4; ++i) {
		const auto cell = QRect(
			rect.x() + (i % 2) * half,
			rect.y() + (i / 2) * half,
			half,
			half);
		if (tracks.empty()) {
			PaintPlaceholder(p, cell, list[index].name + QString::number(i), 0.);
			continue;
		}
		const auto &track = tracks[i % tracks.size()];
		const auto image = state->covers.get(TrackId(track), half, 0);
		if (!image.isNull()) {
			p.drawImage(cell.topLeft(), image);
		} else {
			PaintPlaceholder(p, cell, track.title, 0.);
		}
	}
	p.restore();
}

// ------------------------------------------------------------ track row

class TrackRow final : public Ui::AbstractButton {
public:
	TrackRow(
		QWidget *parent,
		std::shared_ptr<MusicState> state,
		FullMsgId id,
		QString title,
		QString performer,
		int duration,
		QString action)
	: AbstractButton(parent)
	, _state(std::move(state))
	, _id(id)
	, _title(title.isEmpty() ? Utf("Без названия") : title)
	, _performer(performer)
	, _duration(duration) {
		if (!action.isEmpty()) {
			_action = Ui::CreateChild<Ui::LinkButton>(this, action);
		}
		resize(Px(320), Px(58));
		_state->session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
		::Media::Player::instance()->updatedNotifier(
		) | rpl::filter([](const ::Media::Player::TrackState &track) {
			return track.id.type() == AudioMsgId::Type::Song;
		}) | rpl::on_next([=](const ::Media::Player::TrackState &) {
			const auto playing = IsPlaying(_id);
			if (playing != _wasPlaying) {
				_wasPlaying = playing;
				update();
			}
		}, lifetime());
		_wasPlaying = IsPlaying(_id);
		_state->covers.request(_id, crl::guard(this, [=] { update(); }));
	}

	[[nodiscard]] Ui::LinkButton *action() const {
		return _action;
	}

protected:
	int resizeGetHeight(int newWidth) override {
		if (_action) {
			_action->moveToRight(
				Px(14),
				(Px(58) - _action->height()) / 2,
				newWidth);
		}
		return Px(58);
	}

	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto h = height();
		if (_wasPlaying) {
			p.fillRect(rect(), Accent(36));
		} else if (isOver()) {
			p.fillRect(rect(), st::windowBgOver);
		}
		const auto size = Px(44);
		const auto radius = Px(8);
		const auto cover = QRect(Px(12), (h - size) / 2, size, size);
		const auto image = _state->covers.get(_id, size, radius);
		if (!image.isNull()) {
			p.drawImage(cover.topLeft(), image);
		} else {
			PaintPlaceholder(p, cover, _title, radius);
		}
		if (_wasPlaying) {
			// Equalizer bars over the cover of the song that plays.
			p.fillRect(cover, QColor(0, 0, 0, 110));
			const auto bar = Px(4);
			const auto base = cover.bottom() - Px(10);
			const int heights[] = { 10, 18, 13 };
			for (auto i = 0; i != 3; ++i) {
				const auto x = cover.center().x() - Px(9) + i * Px(7);
				p.fillRect(
					QRect(x, base - Px(heights[i]), bar, Px(heights[i])),
					QColor(255, 255, 255));
			}
		}
		if (!_lyricsChecked) {
			if (const auto document = SongOf(_state->session->data().message(_id))) {
				_lyricsChecked = true;
				_lyrics = Lyrics::HasLyrics(document);
			}
		}
		const auto left = cover.x() + size + Px(12);
		auto right = width() - Px(12);
		if (_action) {
			right -= _action->width() + Px(12);
		}
		const auto duration = Playlists::FormatDuration(_duration);
		p.setFont(st::normalFont);
		const auto durationWidth = duration.isEmpty()
			? 0
			: st::normalFont->width(duration);
		if (durationWidth) {
			p.setPen(st::windowSubTextFg);
			p.drawText(
				QRect(right - durationWidth, 0, durationWidth, h),
				Qt::AlignVCenter | Qt::AlignRight,
				duration);
		}
		const auto tag = Utf("ТЕКСТ");
		auto tagFont = st::semiboldFont->f;
		tagFont.setPixelSize(Px(9));
		const auto tagWidth = _lyrics
			? (QFontMetrics(tagFont).horizontalAdvance(tag) + Px(8))
			: 0;
		const auto available = std::max(
			right - durationWidth - Px(10) - left - (tagWidth ? tagWidth + Px(6) : 0),
			Px(20));
		const auto title = st::semiboldFont->elided(_title, available);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		const auto titleBaseline = h / 2 - Px(4);
		p.drawText(left, titleBaseline, title);
		if (tagWidth) {
			const auto x = left + st::semiboldFont->width(title) + Px(6);
			const auto tagRect = QRect(x, titleBaseline - Px(11), tagWidth, Px(13));
			p.setPen(QPen(Accent(170), 1.));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(tagRect, Px(3), Px(3));
			p.setFont(tagFont);
			p.setPen(Accent());
			p.drawText(tagRect, Qt::AlignCenter, tag);
		}
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			left,
			h / 2 + st::normalFont->ascent + Px(2),
			st::normalFont->elided(
				_performer,
				std::max(right - durationWidth - Px(10) - left, Px(20))));
	}

private:
	const std::shared_ptr<MusicState> _state;
	const FullMsgId _id;
	const QString _title;
	const QString _performer;
	const int _duration = 0;
	Ui::LinkButton *_action = nullptr;
	bool _wasPlaying = false;
	bool _lyrics = false;
	bool _lyricsChecked = false;

};

// ------------------------------------------------------------ tiles

class TilesGrid final : public Ui::RpWidget {
public:
	TilesGrid(
		QWidget *parent,
		std::shared_ptr<MusicState> state,
		Fn<void(int)> open,
		Fn<void()> create)
	: RpWidget(parent)
	, _state(std::move(state))
	, _open(std::move(open))
	, _create(std::move(create)) {
		setMouseTracking(true);
		resize(Px(320), Px(200));
		_state->session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
		const auto &list = Playlists::List(_state->session);
		for (const auto &playlist : list) {
			for (auto i = 0; i != std::min(int(playlist.tracks.size()), 4); ++i) {
				_state->covers.request(
					TrackId(playlist.tracks[i]),
					crl::guard(this, [=] { update(); }));
			}
		}
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto count = this->count();
		const auto rows = (count + 1) / 2;
		return rows * (tileSide(newWidth) + Px(52));
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto &list = Playlists::List(_state->session);
		const auto side = tileSide(width());
		for (auto i = 0; i != count(); ++i) {
			const auto rect = tileRect(i);
			const auto cover = QRect(rect.x(), rect.y(), side, side);
			const auto hovered = (i == _over);
			if (i < int(list.size())) {
				PaintCollage(p, hovered ? cover.translated(0, -Px(2)) : cover, _state, i, Px(14));
				const auto &playlist = list[i];
				auto total = 0;
				for (const auto &track : playlist.tracks) {
					total += track.duration;
				}
				p.setFont(st::semiboldFont);
				p.setPen(st::windowFg);
				p.drawText(
					rect.x(),
					cover.bottom() + Px(20),
					st::semiboldFont->elided(playlist.name, side));
				p.setFont(st::normalFont);
				p.setPen(st::windowSubTextFg);
				const auto meta = QString::number(playlist.tracks.size())
					+ Utf(" песен")
					+ (total ? (u" · "_q + Playlists::FormatDuration(total)) : QString());
				p.drawText(
					rect.x(),
					cover.bottom() + Px(38),
					st::normalFont->elided(meta, side));
			} else {
				auto path = QPainterPath();
				path.addRoundedRect(QRectF(cover).adjusted(1, 1, -1, -1), Px(14), Px(14));
				p.fillPath(path, Accent(hovered ? 34 : 20));
				p.setPen(QPen(Accent(140), 1.5, Qt::DashLine));
				p.drawPath(path);
				auto font = st::semiboldFont->f;
				font.setPixelSize(Px(40));
				p.setFont(font);
				p.setPen(Accent());
				p.drawText(cover, Qt::AlignCenter, Utf("+"));
				p.setFont(st::semiboldFont);
				p.setPen(st::windowFg);
				p.drawText(rect.x(), cover.bottom() + Px(20), Utf("Новый плейлист"));
			}
		}
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		const auto over = tileAt(e->pos());
		if (over != _over) {
			_over = over;
			setCursor((_over >= 0) ? style::cur_pointer : style::cur_default);
			update();
		}
	}

	void leaveEventHook(QEvent *e) override {
		if (_over >= 0) {
			_over = -1;
			setCursor(style::cur_default);
			update();
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		_pressed = tileAt(e->pos());
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		const auto pressed = std::exchange(_pressed, -1);
		if (pressed < 0 || pressed != tileAt(e->pos())) {
			return;
		}
		const auto count = int(Playlists::List(_state->session).size());
		// The grid may be destroyed by what the click opens.
		const auto open = _open;
		const auto create = _create;
		if (pressed < count) {
			crl::on_main(this, [=] { open(pressed); });
		} else {
			crl::on_main(this, [=] { create(); });
		}
	}

private:
	[[nodiscard]] int count() const {
		return int(Playlists::List(_state->session).size()) + 1;
	}

	[[nodiscard]] int tileSide(int width) const {
		return std::max((width - Px(14)) / 2, Px(60));
	}

	[[nodiscard]] QRect tileRect(int index) const {
		const auto side = tileSide(width());
		const auto x = (index % 2) * (side + Px(14));
		const auto y = (index / 2) * (side + Px(52));
		return QRect(x, y, side, side + Px(44));
	}

	[[nodiscard]] int tileAt(QPoint point) const {
		for (auto i = 0; i != count(); ++i) {
			if (tileRect(i).contains(point)) {
				return i;
			}
		}
		return -1;
	}

	const std::shared_ptr<MusicState> _state;
	const Fn<void(int)> _open;
	const Fn<void()> _create;
	int _over = -1;
	int _pressed = -1;

};

// ------------------------------------------------------------ hero

class Hero final : public Ui::RpWidget {
public:
	Hero(QWidget *parent, std::shared_ptr<MusicState> state, int index)
	: RpWidget(parent)
	, _state(std::move(state))
	, _index(index) {
		resize(Px(320), Px(124));
		_state->session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return Px(124);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto side = Px(112);
		const auto cover = QRect(0, Px(4), side, side);
		// Soft glow under the cover.
		for (auto i = 3; i > 0; --i) {
			auto path = QPainterPath();
			const auto grow = Px(3) * i;
			path.addRoundedRect(QRectF(cover).adjusted(-grow, -grow, grow, grow), Px(16) + grow, Px(16) + grow);
			p.fillPath(path, Accent(14));
		}
		PaintCollage(p, cover, _state, _index, Px(16));
		const auto &list = Playlists::List(_state->session);
		if (_index < 0 || _index >= int(list.size())) {
			return;
		}
		const auto &playlist = list[_index];
		auto total = 0;
		for (const auto &track : playlist.tracks) {
			total += track.duration;
		}
		const auto left = side + Px(18);
		auto font = st::semiboldFont->f;
		font.setPixelSize(Px(22));
		p.setFont(font);
		p.setPen(st::windowFg);
		p.drawText(
			left,
			Px(48),
			QFontMetrics(font).elidedText(playlist.name, Qt::ElideRight, width() - left));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			left,
			Px(74),
			QString::number(playlist.tracks.size())
				+ Utf(" песен")
				+ (total ? (u" · "_q + Playlists::FormatDuration(total)) : QString()));
	}

private:
	const std::shared_ptr<MusicState> _state;
	const int _index = 0;

};

// ------------------------------------------------------------ now playing

class NowPlaying final : public Ui::AbstractButton {
public:
	NowPlaying(QWidget *parent, std::shared_ptr<MusicState> state)
	: AbstractButton(parent)
	, _state(std::move(state)) {
		resize(Px(320), Px(64));
		::Media::Player::instance()->updatedNotifier(
		) | rpl::filter([](const ::Media::Player::TrackState &track) {
			return track.id.type() == AudioMsgId::Type::Song;
		}) | rpl::on_next([=](const ::Media::Player::TrackState &track) {
			const auto playing = ::Media::Player::ShowPauseIcon(track.state);
			const auto id = track.id.contextId();
			if (playing != _playing || id != _id) {
				_playing = playing;
				_id = id;
				update();
			}
		}, lifetime());
		_state->session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
		const auto current = ::Media::Player::instance()->current(
			AudioMsgId::Type::Song);
		_id = current.contextId();
		_playing = ::Media::Player::ShowPauseIcon(
			::Media::Player::instance()->getState(AudioMsgId::Type::Song).state);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return Px(64);
	}

	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		auto path = QPainterPath();
		path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), Px(14), Px(14));
		p.fillPath(path, Accent(isOver() ? 46 : 26));
		p.setPen(QPen(Accent(90), 1.));
		p.drawPath(path);
		const auto current = ::Media::Player::instance()->current(
			AudioMsgId::Type::Song);
		const auto document = current.audio();
		const auto size = Px(44);
		const auto cover = QRect(Px(10), (height() - size) / 2, size, size);
		auto title = Utf("Сейчас ничего не играет");
		auto performer = Utf("Нажми на песню в плейлисте или в поиске");
		if (document) {
			const auto image = _state->covers.get(current.contextId(), size, Px(8));
			if (const auto song = document->song()) {
				title = song->title.isEmpty() ? document->filename() : song->title;
				performer = song->performer;
			}
			if (!image.isNull()) {
				p.drawImage(cover.topLeft(), image);
			} else {
				PaintPlaceholder(p, cover, title, Px(8));
			}
		} else {
			PaintPlaceholder(p, cover, title, Px(8));
		}
		const auto left = cover.x() + size + Px(12);
		const auto hint = document ? Utf("≡ Текст") : QString();
		p.setFont(st::semiboldFont);
		const auto hintWidth = hint.isEmpty() ? 0 : st::semiboldFont->width(hint);
		const auto available = std::max(width() - left - hintWidth - Px(24), Px(20));
		p.setPen(st::windowFg);
		p.drawText(left, height() / 2 - Px(4), st::semiboldFont->elided(title, available));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			left,
			height() / 2 + st::normalFont->ascent + Px(2),
			st::normalFont->elided(
				(_playing ? Utf("▶ ") : (document ? Utf("⏸ ") : QString())) + performer,
				available));
		if (hintWidth) {
			p.setFont(st::semiboldFont);
			p.setPen(Accent());
			p.drawText(
				QRect(width() - hintWidth - Px(14), 0, hintWidth, height()),
				Qt::AlignVCenter | Qt::AlignRight,
				hint);
		}
	}

private:
	const std::shared_ptr<MusicState> _state;
	FullMsgId _id;
	bool _playing = false;

};

void AskName(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &value,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Название")),
			value));
		field->setMaxLength(64);
		box->setFocusCallback([=] { field->setFocusFast(); });
		const auto submit = [=] {
			const auto text = field->getLastText().trimmed();
			if (text.isEmpty()) {
				field->showError();
				return;
			}
			done(text);
			box->closeBox();
		};
		field->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers) {
			submit();
		}, field->lifetime());
		box->addButton(rpl::single(Utf("Готово")), submit);
		box->addButton(rpl::single(Utf("Отмена")), [=] { box->closeBox(); });
	}));
}

enum class ViewKind {
	Home,
	Playlist,
	Search,
};

struct Navigation {
	ViewKind kind = ViewKind::Home;
	int index = -1;
	int generation = 0;
	Fn<void()> home;
	Fn<void(int)> playlist;
	Fn<void(QString)> search;
	Fn<void()> refresh;
};

} // namespace

void Show(not_null<Window::SessionController*> controller) {
	const auto state = std::make_shared<MusicState>(controller);
	const auto session = state->session;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth + Px(150));
		const auto title = box->lifetime().make_state<rpl::variable<QString>>(
			Utf("Музыка"));
		box->setTitle(title->value());

		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Utf("Поиск песен по Telegram"))));
		const auto now = box->addRow(
			object_ptr<NowPlaying>(box, state),
			style::margins(
				st::boxRowPadding.left(),
				Px(8),
				st::boxRowPadding.right(),
				Px(4)));
		now->setClickedCallback([=] {
			Lyrics::ShowLyricsList(controller);
		});
		const auto content = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		const auto nav = std::make_shared<Navigation>();

		const auto addLabel = [=](const QString &text) {
			content->add(
				object_ptr<Ui::FlatLabel>(content, text, st::boxDividerLabel),
				style::margins(
					st::boxRowPadding.left(),
					Px(10),
					st::boxRowPadding.right(),
					Px(6)));
		};
		const auto createPlaylist = [=] {
			AskName(controller, Utf("Новый плейлист"), QString(), [=](QString name) {
				Playlists::Create(session, name);
			});
		};

		nav->home = [=] {
			nav->kind = ViewKind::Home;
			nav->index = -1;
			*title = Utf("Музыка");
			content->clear();
			addLabel(Utf("ПЛЕЙЛИСТЫ"));
			content->add(
				object_ptr<TilesGrid>(
					content,
					state,
					[=](int index) { nav->playlist(index); },
					createPlaylist),
				st::boxRowPadding);
			if (Playlists::List(session).empty()) {
				addLabel(Utf("Найди песню через поиск сверху и добавь её «+», "
					"или нажми правой кнопкой на трек в чате → «В плейлист MWTK…»."));
			}
			box->clearButtons();
			box->addButton(rpl::single(Utf("Закрыть")), [=] { box->closeBox(); });
		};

		nav->playlist = [=](int index) {
			const auto &list = Playlists::List(session);
			if (index < 0 || index >= int(list.size())) {
				nav->home();
				return;
			}
			nav->kind = ViewKind::Playlist;
			nav->index = index;
			*title = list[index].name;
			content->clear();
			content->add(
				object_ptr<Hero>(content, state, index),
				style::margins(
					st::boxRowPadding.left(),
					Px(6),
					st::boxRowPadding.right(),
					Px(4)));

			auto actionsOwner = object_ptr<Ui::RpWidget>(content);
			actionsOwner->resize(Px(320), Px(30));
			const auto actions = content->add(
				std::move(actionsOwner),
				style::margins(
					st::boxRowPadding.left(),
					0,
					st::boxRowPadding.right(),
					Px(6)));
			const auto rename = Ui::CreateChild<Ui::LinkButton>(
				actions,
				Utf("Переименовать"));
			const auto remove = Ui::CreateChild<Ui::LinkButton>(
				actions,
				Utf("Удалить плейлист"));
			actions->widthValue(
			) | rpl::on_next([=](int width) {
				rename->moveToLeft(0, Px(4), width);
				remove->moveToLeft(rename->width() + Px(24), Px(4), width);
			}, actions->lifetime());
			rename->setClickedCallback([=] {
				const auto &list = Playlists::List(session);
				if (index < int(list.size())) {
					AskName(controller, Utf("Переименовать"), list[index].name, [=](QString name) {
						Playlists::Rename(session, index, name);
					});
				}
			});
			remove->setClickedCallback([=] {
				crl::on_main(box, [=] {
					nav->kind = ViewKind::Home;
					Playlists::Remove(session, index);
				});
			});

			const auto &tracks = list[index].tracks;
			if (tracks.empty()) {
				addLabel(Utf("Плейлист пуст. Найди песню через поиск сверху "
					"и нажми «+»."));
			}
			for (auto i = 0; i != int(tracks.size()); ++i) {
				const auto &track = tracks[i];
				const auto row = content->add(object_ptr<TrackRow>(
					content,
					state,
					TrackId(track),
					track.title,
					track.performer,
					track.duration,
					Utf("✕")));
				row->setClickedCallback([=] {
					Playlists::Play(session, index, i, false);
				});
				row->action()->setClickedCallback([=] {
					crl::on_main(box, [=] {
						Playlists::RemoveTrack(session, index, i);
					});
				});
			}
			box->clearButtons();
			box->addLeftButton(rpl::single(Utf("← Назад")), [=] {
				crl::on_main(box, [=] { nav->home(); });
			});
			box->addButton(rpl::single(Utf("Слушать")), [=] {
				Playlists::Play(session, index, 0, false);
			});
			box->addButton(rpl::single(Utf("Перемешать")), [=] {
				Playlists::Play(session, index, -1, true);
			});
		};

		nav->search = [=](QString query) {
			nav->kind = ViewKind::Search;
			const auto generation = ++nav->generation;
			*title = Utf("Поиск");
			content->clear();
			const auto status = content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					Utf("Ищу в каналах Telegram…"),
					st::boxDividerLabel),
				style::margins(
					st::boxRowPadding.left(),
					Px(10),
					st::boxRowPadding.right(),
					Px(6)));
			box->clearButtons();
			box->addLeftButton(rpl::single(Utf("← Назад")), [=] {
				crl::on_main(box, [=] {
					field->clear();
					nav->home();
				});
			});
			box->addButton(rpl::single(Utf("Закрыть")), [=] { box->closeBox(); });
			session->api().requestGlobalMedia(
				Storage::SharedMediaType::MusicFile,
				query,
				0,
				Data::MessagePosition(),
				false,
				crl::guard(box, [=](Api::GlobalMediaResult result) {
					if (nav->generation != generation
						|| nav->kind != ViewKind::Search) {
						return;
					}
					auto found = 0;
					for (const auto &position : result.messageIds) {
						const auto id = position.fullId;
						const auto document = SongOf(session->data().message(id));
						if (!document) {
							continue;
						}
						auto songTitle = QString();
						auto performer = QString();
						if (const auto song = document->song()) {
							songTitle = song->title;
							performer = song->performer;
						}
						if (songTitle.isEmpty()) {
							songTitle = document->filename();
						}
						const auto row = content->add(object_ptr<TrackRow>(
							content,
							state,
							id,
							songTitle,
							performer,
							int(document->duration() / 1000),
							Utf("+ в плейлист")));
						row->setClickedCallback([=] {
							if (const auto song = SongOf(session->data().message(id))) {
								::Media::Player::instance()->play(AudioMsgId(song, id));
							}
						});
						row->action()->setClickedCallback([=] {
							Playlists::ShowAddToPlaylistBox(controller, id);
						});
						++found;
					}
					status->setText(found
						? Utf("Нажми на песню, чтобы слушать, «+ в плейлист» — чтобы сохранить.")
						: Utf("Ничего не нашлось. Попробуй написать иначе."));
				}));
		};

		nav->refresh = [=] {
			if (nav->kind == ViewKind::Home) {
				nav->home();
			} else if (nav->kind == ViewKind::Playlist) {
				nav->playlist(nav->index);
			}
		};
		Playlists::Changes(
		) | rpl::on_next([=] {
			crl::on_main(box, [=] { nav->refresh(); });
		}, box->lifetime());

		field->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers) {
			const auto query = field->getLastText().trimmed();
			if (!query.isEmpty()) {
				nav->search(query);
			}
		}, field->lifetime());
		box->setFocusCallback([=] { field->setFocusFast(); });

		nav->home();
	}));
}

} // namespace Mwtk::Music
