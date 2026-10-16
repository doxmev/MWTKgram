/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_music.h"

#include "apiwrap.h"
#include "api/api_common.h"
#include "api/api_sending.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/data_audio_msg_id.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_messages.h"
#include "data/data_peer_id.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "inline_bots/inline_bot_result.h"
#include "main/main_app_config.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "mwtk/mwtk_lyrics.h"
#include "mwtk/mwtk_playlists.h"
#include "mwtk/mwtk_taste.h"
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

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtGui/QLinearGradient>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <map>
#include <random>

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

	// Takes no space in the list any more (a disliked song).
	void collapse() {
		_collapsed = true;
		hide();
		resizeToWidth(width());
	}

	// A second button left of the main one («👎» in «Мой микс»).
	Ui::LinkButton *addExtraAction(const QString &text) {
		_extra = Ui::CreateChild<Ui::LinkButton>(this, text);
		_extra->show();
		resizeToWidth(width());
		return _extra;
	}

protected:
	int resizeGetHeight(int newWidth) override {
		if (_collapsed) {
			return 0;
		}
		auto skip = Px(14);
		if (_action) {
			_action->moveToRight(
				skip,
				(Px(58) - _action->height()) / 2,
				newWidth);
			skip += _action->width() + Px(14);
		}
		if (_extra) {
			_extra->moveToRight(
				skip,
				(Px(58) - _extra->height()) / 2,
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
		if (_extra) {
			right -= _extra->width() + Px(14);
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
	Ui::LinkButton *_extra = nullptr;
	bool _collapsed = false;
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

// ------------------------------------------------------------ music bot
// The official Telegram music search (the bot from the app config, the one
// the "attach music" box uses): finds songs everywhere, not only in chats
// the account is in.

void ResolveMusicBot(
		not_null<Main::Session*> session,
		Fn<void(UserData*)> done) {
	const auto username = session->appConfig().get<QString>(
		u"music_search_username"_q,
		QString());
	if (username.isEmpty()) {
		done(nullptr);
		return;
	}
	if (const auto peer = session->data().peerByUsername(username)) {
		done(peer->asUser());
		return;
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done(crl::guard(session, [=](const MTPcontacts_ResolvedPeer &result) {
		auto bot = (UserData*)nullptr;
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			const auto peer = session->data().peerLoaded(
				peerFromMTP(data.vpeer()));
			bot = peer ? peer->asUser() : nullptr;
		});
		done(bot);
	})).fail(crl::guard(session, [=](const MTP::Error &) {
		done(nullptr);
	})).send();
}

// Calls done() with songs as local (not saved) messages, empty on failure.
void SearchBot(
		not_null<Main::Session*> session,
		const QString &query,
		Fn<void(std::vector<FullMsgId>)> done) {
	ResolveMusicBot(session, [=](UserData *bot) {
		if (!bot) {
			done({});
			return;
		}
		session->api().request(MTPmessages_GetInlineBotResults(
			MTP_flags(0),
			bot->inputUser(),
			session->user()->input(),
			MTPInputGeoPoint(),
			MTP_string(query),
			MTP_string()
		)).done(crl::guard(session, [=](const MTPmessages_BotResults &result) {
			auto ids = std::vector<FullMsgId>();
			if (result.type() != mtpc_messages_botResults) {
				done(std::move(ids));
				return;
			}
			const auto &data = result.c_messages_botResults();
			session->data().processUsers(data.vusers());
			const auto history = session->data().history(session->user());
			const auto queryId = data.vquery_id().v;
			for (const auto &item : data.vresults().v) {
				const auto parsed = InlineBots::Result::Create(
					session,
					queryId,
					item);
				const auto document = parsed ? parsed->document() : nullptr;
				if (!document
					|| !document->isSong()
					|| !parsed->contentUrl().isEmpty()) {
					continue;
				}
				// The history owns the local message until the session ends.
				const auto message = parsed->makeMessage(history, {
					.id = history->nextNonHistoryEntryId(),
					.flags = (MessageFlag::FakeHistoryItem
						| MessageFlag::HasFromId),
					.from = history->peer->id,
					.date = base::unixtime::now(),
					.viaBotId = peerToUser(bot->id),
				});
				ids.push_back(message->fullId());
			}
			done(std::move(ids));
		})).fail(crl::guard(session, [=](const MTP::Error &) {
			done({});
		})).send();
	});
}

// A song from the bot is not a real message, so it is saved to
// «Избранное» first and the saved message goes to the playlist.
void SaveAndAddToPlaylist(
		not_null<Window::SessionController*> controller,
		FullMsgId id) {
	const auto session = &controller->session();
	const auto item = session->data().message(id);
	if (!item) {
		return;
	} else if (item->isRegular()) {
		Playlists::ShowAddToPlaylistBox(controller, id);
		return;
	}
	const auto document = SongOf(item);
	if (!document) {
		return;
	}
	const auto history = session->data().history(session->user());
	const auto lifetime = std::make_shared<rpl::lifetime>();
	const auto weak = base::make_weak(controller);
	session->data().itemIdChanged(
	) | rpl::on_next([=](Data::Session::IdChange change) {
		const auto strong = weak.get();
		if (!strong) {
			lifetime->destroy();
			return;
		}
		const auto sent = session->data().message(change.newId);
		if (sent && sent->history() == history && SongOf(sent) == document) {
			lifetime->destroy();
			Playlists::ShowAddToPlaylistBox(strong, change.newId);
		}
	}, *lifetime);
	Api::SendExistingDocument(
		Api::MessageToSend(Api::SendAction(history)),
		document);
	controller->showToast(Utf("Сохраняю песню в «Избранное»…"));
}

void GetJson(const QString &url, Fn<void(QJsonObject)> callback) {
	// Lives until the app exits, like the lyrics network manager.
	static const auto network = new QNetworkAccessManager();
	auto request = QNetworkRequest(QUrl(url));
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"MWTKgram (https://github.com/doxmev/MWTKgram)"_q);
	request.setTransferTimeout(12000);
	const auto reply = network->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		callback((reply->error() == QNetworkReply::NoError)
			? QJsonDocument::fromJson(reply->readAll()).object()
			: QJsonObject());
	});
}

struct MixCandidate {
	QString artist;
	QString title;
	bool familiar = false;
};

// Candidates from the open Deezer catalog (no key needed), like a VK or
// Spotify mix: mostly songs of artists related to the ones you listen to,
// plus a few songs of your own artists. Disliked songs, banned artists and
// songs played in the last days are left out.
void FindMixCandidates(
		std::vector<QString> seeds,
		Fn<void(std::vector<MixCandidate>)> done) {
	struct Collect {
		std::vector<QString> used;
		std::vector<MixCandidate> result;
		int pending = 0;
		Fn<void(std::vector<MixCandidate>)> done;
	};
	const auto state = std::make_shared<Collect>();
	state->done = std::move(done);
	for (const auto &seed : seeds) {
		state->used.push_back(seed.toLower());
	}
	const auto finishOne = [=] {
		if (--state->pending == 0) {
			auto result = std::move(state->result);
			auto random = std::mt19937(uint32(base::RandomValue<uint32>()));
			std::shuffle(result.begin(), result.end(), random);
			state->done(std::move(result));
		}
	};
	const auto encode = [](const QString &text) {
		return QString::fromLatin1(QUrl::toPercentEncoding(text));
	};
	// Top songs of an artist: `take` random ones that pass the filters.
	const auto addTop = [=](qint64 id, QString name, int take, bool familiar) {
		++state->pending;
		GetJson(
			u"https://api.deezer.com/artist/%1/top?limit=15"_q.arg(id),
			[=](QJsonObject top) {
			auto songs = top.value(u"data"_q).toArray();
			auto order = std::vector<int>();
			for (auto k = 0; k != songs.size(); ++k) {
				order.push_back(k);
			}
			auto random = std::mt19937(uint32(base::RandomValue<uint32>()));
			std::shuffle(order.begin(), order.end(), random);
			auto taken = 0;
			for (const auto k : order) {
				if (taken >= take) {
					break;
				}
				const auto title = songs.at(k).toObject().value(
					u"title"_q).toString();
				if (title.isEmpty()
					|| Taste::IsDisliked(name, title)
					|| Taste::PlayedRecently(name, title, 3)) {
					continue;
				}
				const auto duplicate = ranges::any_of(
					state->result,
					[&](const MixCandidate &c) {
						return !c.title.compare(title, Qt::CaseInsensitive)
							&& !c.artist.compare(name, Qt::CaseInsensitive);
					});
				if (duplicate) {
					continue;
				}
				state->result.push_back({ name, title, familiar });
				++taken;
			}
			finishOne();
		});
	};
	const auto count = int(seeds.size());
	state->pending = count;
	for (auto i = 0; i != count; ++i) {
		const auto seed = seeds[i];
		GetJson(
			u"https://api.deezer.com/search/artist?limit=1&q="_q + encode(seed),
			[=](QJsonObject found) {
			const auto data = found.value(u"data"_q).toArray();
			if (data.isEmpty()) {
				finishOne();
				return;
			}
			const auto id = qint64(
				data.at(0).toObject().value(u"id"_q).toDouble());
			// Every second seed also gives one song of its own: the mix
			// stays recognisable, like VK mixes do.
			if (i % 2 == 0) {
				addTop(id, seed, 1, true);
			}
			++state->pending;
			GetJson(
				u"https://api.deezer.com/artist/%1/related?limit=20"_q.arg(id),
				[=](QJsonObject related) {
				auto artists = related.value(u"data"_q).toArray();
				auto order = std::vector<int>();
				for (auto k = 0; k != artists.size(); ++k) {
					order.push_back(k);
				}
				auto random = std::mt19937(uint32(base::RandomValue<uint32>()));
				std::shuffle(order.begin(), order.end(), random);
				auto taken = 0;
				for (const auto k : order) {
					if (taken >= 3) {
						break;
					}
					const auto artist = artists.at(k).toObject();
					const auto name = artist.value(u"name"_q).toString();
					if (name.isEmpty()
						|| Taste::ArtistBanned(name)
						|| ranges::contains(state->used, name.toLower())) {
						continue;
					}
					state->used.push_back(name.toLower());
					++taken;
					addTop(
						qint64(artist.value(u"id"_q).toDouble()),
						name,
						2,
						false);
				}
				finishOne();
			});
			finishOne();
		});
	}
	if (!count) {
		state->done({});
	}
}

// «Мой микс»: built from everything you listen to (history with skips,
// playlists, the song playing now), not only the current song.
void BuildMix(
		not_null<Main::Session*> session,
		Fn<void(std::vector<FullMsgId>, bool)> done) {
	auto extra = std::vector<QString>();
	const auto current = ::Media::Player::instance()->current(
		AudioMsgId::Type::Song);
	if (const auto document = current.audio()) {
		if (const auto song = document->song()) {
			extra.push_back(song->performer);
		}
	}
	for (const auto &playlist : Playlists::List(session)) {
		for (const auto &track : playlist.tracks) {
			extra.push_back(track.performer);
		}
	}
	const auto seeds = Taste::SeedArtists(5, extra);
	if (seeds.empty()) {
		done({}, false);
		return;
	}
	FindMixCandidates(seeds, crl::guard(session, [=](
			std::vector<MixCandidate> candidates) {
		auto queries = std::vector<QString>();
		for (const auto &candidate : candidates) {
			queries.push_back(candidate.artist + ' ' + candidate.title);
		}
		const auto exact = !queries.empty();
		if (!exact) {
			queries = seeds; // Deezer is not reachable: your own artists.
		}
		if (queries.size() > size_t(exact ? 22 : 6)) {
			queries.resize(exact ? 22 : 6);
		}
		struct Collected {
			std::vector<FullMsgId> ids;
			int waiting = 0;
		};
		const auto collected = std::make_shared<Collected>();
		collected->waiting = int(queries.size());
		const auto accept = [=](FullMsgId id) {
			const auto document = SongOf(session->data().message(id));
			const auto song = document ? document->song() : nullptr;
			if (!song
				|| Taste::IsDisliked(song->performer, song->title)
				|| Taste::ArtistBanned(song->performer)) {
				return false;
			}
			for (const auto &other : collected->ids) {
				const auto was = SongOf(session->data().message(other));
				const auto wasSong = was ? was->song() : nullptr;
				if (wasSong
					&& !wasSong->title.compare(song->title, Qt::CaseInsensitive)
					&& !wasSong->performer.compare(
						song->performer,
						Qt::CaseInsensitive)) {
					return false;
				}
			}
			return true;
		};
		for (const auto &query : queries) {
			SearchBot(session, query, [=](std::vector<FullMsgId> ids) {
				if (exact) {
					for (const auto &id : ids) {
						if (accept(id)) {
							collected->ids.push_back(id); // best match
							break;
						}
					}
				} else {
					auto random = std::mt19937(
						uint32(base::RandomValue<uint32>()));
					std::shuffle(ids.begin(), ids.end(), random);
					auto taken = 0;
					for (const auto &id : ids) {
						if (taken < 4 && accept(id)) {
							collected->ids.push_back(id);
							++taken;
						}
					}
				}
				if (--collected->waiting == 0) {
					auto all = std::move(collected->ids);
					auto random = std::mt19937(
						uint32(base::RandomValue<uint32>()));
					std::shuffle(all.begin(), all.end(), random);
					done(std::move(all), true);
				}
			});
		}
	}));
}

enum class ViewKind {
	Home,
	Playlist,
	Search,
	Mix,
};

struct Navigation {
	ViewKind kind = ViewKind::Home;
	int index = -1;
	int generation = 0;
	Fn<void()> home;
	Fn<void(int)> playlist;
	Fn<void(QString)> search;
	Fn<void()> mix;
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
			rpl::single(Utf("Поиск песен: исполнитель или название"))));
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
			const auto mixButton = content->add(
				object_ptr<Ui::RoundButton>(
					content,
					rpl::single(Utf("▶  Мой микс")),
					st::defaultActiveButton),
				style::margins(
					st::boxRowPadding.left(),
					Px(8),
					st::boxRowPadding.right(),
					Px(2)),
				style::al_left);
			mixButton->setClickedCallback([=] {
				crl::on_main(box, [=] { nav->mix(); });
			});
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

		// Rows of found songs: a click plays the list from that song on.
		const auto addFoundRows = [=](
				std::vector<FullMsgId> ids,
				bool withDislike) {
			auto added = 0;
			for (auto i = 0; i != int(ids.size()); ++i) {
				const auto id = ids[i];
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
					Playlists::PlayQueue(session, ids, i);
				});
				row->action()->setClickedCallback([=] {
					SaveAndAddToPlaylist(controller, id);
				});
				if (withDislike) {
					const auto dislike = row->addExtraAction(Utf("👎"));
					dislike->setClickedCallback([=] {
						Taste::Dislike(performer, songTitle);
						if (IsPlaying(id)) {
							::Media::Player::instance()->next(
								AudioMsgId::Type::Song);
						}
						crl::on_main(row, [=] { row->collapse(); });
						controller->showToast(Utf(
							"Больше не будет в миксе. Два «👎» одному исполнителю — "
							"и он тоже пропадёт."));
					});
				}
				++added;
			}
			return added;
		};
		const auto addStatus = [=](const QString &text) {
			return content->add(
				object_ptr<Ui::FlatLabel>(content, text, st::boxDividerLabel),
				style::margins(
					st::boxRowPadding.left(),
					Px(10),
					st::boxRowPadding.right(),
					Px(6)));
		};
		const auto setBackButtons = [=] {
			box->clearButtons();
			box->addLeftButton(rpl::single(Utf("← Назад")), [=] {
				crl::on_main(box, [=] {
					field->clear();
					nav->home();
				});
			});
			box->addButton(rpl::single(Utf("Закрыть")), [=] { box->closeBox(); });
		};

		nav->search = [=](QString query) {
			nav->kind = ViewKind::Search;
			const auto generation = ++nav->generation;
			*title = Utf("Поиск");
			content->clear();
			const auto status = addStatus(Utf("Ищу в музыке Telegram…"));
			setBackButtons();
			const auto alive = [=] {
				return (nav->generation == generation)
					&& (nav->kind == ViewKind::Search);
			};
			const auto found = std::make_shared<int>(0);
			const auto searchChats = [=] {
				session->api().requestGlobalMedia(
					Storage::SharedMediaType::MusicFile,
					query,
					0,
					Data::MessagePosition(),
					false,
					crl::guard(box, [=](Api::GlobalMediaResult result) {
						if (!alive()) {
							return;
						}
						auto ids = std::vector<FullMsgId>();
						for (const auto &position : result.messageIds) {
							ids.push_back(position.fullId);
						}
						if (!ids.empty()) {
							addStatus(Utf("ИЗ ТВОИХ ЧАТОВ"));
						}
						*found += addFoundRows(std::move(ids), false);
						status->setText(*found
							? Utf("Нажми на песню — заиграет список с неё, «+ в плейлист» — сохранить.")
							: Utf("Ничего не нашлось. Попробуй написать иначе."));
					}));
			};
			SearchBot(session, query, crl::guard(box, [=](
					std::vector<FullMsgId> ids) {
				if (!alive()) {
					return;
				}
				*found += addFoundRows(std::move(ids), false);
				searchChats();
			}));
		};

		nav->mix = [=] {
			nav->kind = ViewKind::Mix;
			const auto generation = ++nav->generation;
			*title = Utf("Мой микс");
			content->clear();
			const auto status = addStatus(Utf("Подбираю музыку, похожую на твою…"));
			setBackButtons();
			BuildMix(session, crl::guard(box, [=](
					std::vector<FullMsgId> ids,
					bool hadArtists) {
				if (nav->generation != generation || nav->kind != ViewKind::Mix) {
					return;
				}
				if (!addFoundRows(ids, true)) {
					status->setText(hadArtists
						? Utf("Музыкальный поиск Telegram сейчас не ответил. Попробуй ещё раз.")
						: Utf("Добавь песни в плейлисты или включи песню — микс подберётся по ним."));
					return;
				}
				status->setText(Utf("Похожие исполнители на тех, кого ты слушаешь. Каждый раз новый микс."));
				box->clearButtons();
				box->addLeftButton(rpl::single(Utf("← Назад")), [=] {
					crl::on_main(box, [=] { nav->home(); });
				});
				box->addButton(rpl::single(Utf("Ещё микс")), [=] {
					crl::on_main(box, [=] { nav->mix(); });
				});
				box->addButton(rpl::single(Utf("Слушать")), [=] {
					Playlists::PlayQueue(session, ids, 0);
				});
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
