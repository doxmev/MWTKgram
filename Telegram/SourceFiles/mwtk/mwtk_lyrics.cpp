/*
This file is part of MWTKgram, a Telegram Desktop based client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mwtk/mwtk_lyrics.h"

#include "base/event_filter.h"
#include "base/options.h"
#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "ui/image/image.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "mwtk/mwtk_options.h"
#include "mwtk/mwtk_room.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QUrlQuery>
#include <QtGui/QFontDatabase>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

#include <cmath>
#include <optional>
#include <random>

namespace Mwtk::Lyrics {
namespace {

constexpr auto kFlyIn = crl::time(550);
constexpr auto kAppearAhead = crl::time(450);
constexpr auto kVanish = crl::time(600);
constexpr auto kMaxParticles = 900;
constexpr auto kNotFoundRetry = 7 * 24 * 3600;

// ---------------------------------------------------------------- storage

[[nodiscard]] QString CacheDir() {
	return cWorkingDir() + u"tdata/mwtk_lyrics/"_q;
}

[[nodiscard]] QString HashName(const QString &key) {
	return QString::fromLatin1(QCryptographicHash::hash(
		key.toUtf8(),
		QCryptographicHash::Sha1).toHex());
}

struct TrackInfo {
	DocumentData *document = nullptr;
	QString key;
	QString title;
	QString performer;
	crl::time duration = 0;
};

[[nodiscard]] TrackInfo InfoFor(DocumentData *document) {
	auto result = TrackInfo();
	result.document = document;
	if (!document) {
		return result;
	}
	result.duration = document->duration();
	if (const auto song = document->song()) {
		result.title = song->title.trimmed();
		result.performer = song->performer.trimmed();
	}
	if (result.title.isEmpty()) {
		auto name = QFileInfo(document->filename()).completeBaseName();
		const auto dash = name.indexOf(u" - "_q);
		if (dash > 0) {
			result.performer = name.left(dash).trimmed();
			result.title = name.mid(dash + 3).trimmed();
		} else {
			result.title = name.trimmed();
		}
	}
	result.key = result.performer.toLower()
		+ u" | "_q
		+ result.title.toLower()
		+ u" | "_q
		+ QString::number(result.duration / 1000);
	return result;
}

[[nodiscard]] QString CachePath(const TrackInfo &info, const QString &ext) {
	return CacheDir() + HashName(info.key) + ext;
}

[[nodiscard]] QJsonObject ReadOffsets() {
	auto file = QFile(CacheDir() + u"offsets.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

void WriteOffsets(const QJsonObject &object) {
	QDir().mkpath(CacheDir());
	auto file = QSaveFile(CacheDir() + u"offsets.json"_q);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void WriteText(const QString &path, const QString &text) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(text.toUtf8());
		file.commit();
	}
}

[[nodiscard]] QString ReadText(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	return QString::fromUtf8(file.readAll());
}

// ---------------------------------------------------------------- state

struct State {
	TrackInfo track;
	std::vector<Line> lines;
	crl::time offset = 0;
	bool loading = false;
	int generation = 0;
	rpl::event_stream<> changes;
	std::unique_ptr<QNetworkAccessManager> network;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

void SetLines(int generation, std::vector<Line> lines) {
	auto &state = Current();
	if (state.generation != generation) {
		return;
	}
	state.loading = false;
	state.lines = std::move(lines);
	state.changes.fire({});
}

void FetchOnline(const TrackInfo &info, int generation);

void Load(DocumentData *document) {
	auto &state = Current();
	++state.generation;
	state.track = InfoFor(document);
	state.lines.clear();
	state.loading = false;
	state.offset = 0;
	state.changes.fire({});
	if (!document || state.track.title.isEmpty()) {
		return;
	}
	const auto generation = state.generation;
	state.offset = crl::time(ReadOffsets().value(state.track.key).toInt());

	// 1. A .lrc file next to the downloaded song.
	const auto path = document->filepath(true);
	if (!path.isEmpty()) {
		const auto info = QFileInfo(path);
		const auto lrc = info.absolutePath()
			+ '/'
			+ info.completeBaseName()
			+ u".lrc"_q;
		if (QFile::exists(lrc)) {
			auto lines = ParseLrc(ReadText(lrc));
			if (!lines.empty()) {
				SetLines(generation, std::move(lines));
				return;
			}
		}
	}
	// 2. Our cache (downloaded before or made in the sync editor).
	const auto cached = CachePath(state.track, u".lrc"_q);
	if (QFile::exists(cached)) {
		auto lines = ParseLrc(ReadText(cached));
		if (!lines.empty()) {
			SetLines(generation, std::move(lines));
			return;
		}
	}
	// 3. LRCLIB, unless we recently learned it has nothing.
	const auto none = CachePath(state.track, u".none2"_q);
	if (QFile::exists(none)) {
		const auto age = QFileInfo(none).lastModified().secsTo(
			QDateTime::currentDateTime());
		if (age < kNotFoundRetry) {
			return;
		}
	}
	if (OptionValue(kOptionLyricsOnline)) {
		state.loading = true;
		FetchOnline(state.track, generation);
	}
}

[[nodiscard]] QNetworkRequest MakeRequest(const QUrl &url) {
	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"MWTKgram (https://github.com/doxmev/MWTKgram)"_q);
	request.setTransferTimeout(15000);
	return request;
}

// 1 sped up, -1 slowed, 0 normal.
[[nodiscard]] int SpeedOf(const QString &title) {
	const auto t = title.toLower();
	if (t.contains(u"sped"_q)
		|| t.contains(u"speed up"_q)
		|| t.contains(u"nightcore"_q)
		|| t.contains(QString::fromUtf8("ускор"))) {
		return 1;
	} else if (t.contains(u"slow"_q) || t.contains(QString::fromUtf8("замедл"))) {
		return -1;
	}
	return 0;
}

// "Song (sped up)", "Song [Official Audio]", "Song feat. X" -> "Song".
[[nodiscard]] QString CleanTitle(const QString &title) {
	static const auto variant = QRegularExpression(
		uR"(\s*[(\[][^)\]]*(sped|speed|slow|nightcore|remix|ver|version|edit|prod|feat|ft\.|lyric|audio|official|cover|instrumental|mood|tiktok|reverb|bass|8d)[^)\]]*[)\]])"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto feat = QRegularExpression(
		uR"(\s+(feat\.?|ft\.?)\s.*$)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto dash = QRegularExpression(
		uR"(\s*-\s*(sped up|slowed|nightcore).*$)"_q,
		QRegularExpression::CaseInsensitiveOption);
	auto result = title;
	result.replace(variant, QString());
	result.replace(feat, QString());
	result.replace(dash, QString());
	result = result.trimmed();
	return result.isEmpty() ? title.trimmed() : result;
}

[[nodiscard]] QString ScaleLrc(const QString &lrc, double factor) {
	if (std::abs(factor - 1.) < 0.01) {
		return lrc;
	}
	auto lines = ParseLrc(lrc);
	for (auto &line : lines) {
		line.at = crl::time(std::round(line.at * factor));
		line.end = crl::time(std::round(line.end * factor));
		for (auto &time : line.wordTimes) {
			time = crl::time(std::round(time * factor));
		}
	}
	return SerializeLrc(lines);
}

void FinishFetch(
		const TrackInfo &info,
		int generation,
		const QString &lrc,
		bool reached) {
	if (lrc.isEmpty()) {
		if (reached) {
			// The site answered and has nothing. A network error is not
			// remembered: next time the song is played we try again.
			WriteText(CachePath(info, u".none2"_q), QString());
		}
		SetLines(generation, {});
		return;
	}
	WriteText(CachePath(info, u".lrc"_q), lrc);
	SetLines(generation, ParseLrc(lrc));
}

[[nodiscard]] QString PickBest(const QJsonArray &list, const TrackInfo &info) {
	const auto speed = SpeedOf(info.title);
	const auto title = CleanTitle(info.title).toLower();
	const auto artist = info.performer.trimmed().toLower();
	const auto duration = info.duration / 1000.;
	auto best = QString();
	auto bestScore = 1e9;
	auto bestFactor = 1.;
	for (const auto &value : list) {
		const auto object = value.toObject();
		const auto synced = object.value(u"syncedLyrics"_q).toString();
		if (synced.isEmpty()) {
			continue;
		}
		const auto itemTitle = object.value(u"trackName"_q).toString().toLower();
		const auto itemArtist = object.value(u"artistName"_q).toString().toLower();
		if (!itemTitle.contains(title) && !title.contains(itemTitle)) {
			continue;
		}
		const auto penalty = (artist.isEmpty()
			|| itemArtist.contains(artist)
			|| artist.contains(itemArtist)) ? 0. : 50.;
		const auto d = object.value(u"duration"_q).toDouble();
		auto factor = 1.;
		auto score = 0.;
		if (duration <= 0. || d <= 0.) {
			score = 30.;
		} else if (std::abs(d - duration) <= 3.) {
			score = std::abs(d - duration);
		} else if (speed != 0) {
			// Sped up / slowed edit: same words on a stretched timeline.
			const auto ratio = d / duration;
			const auto fits = (speed > 0)
				? (ratio > 1.05 && ratio < 1.7)
				: (ratio > 0.55 && ratio < 0.95);
			if (!fits) {
				continue;
			}
			factor = duration / d;
			score = 5. + std::abs(ratio - (speed > 0 ? 1.25 : 0.8)) * 20.;
		} else if (std::abs(d - duration) <= 9.) {
			score = 10. + std::abs(d - duration);
		} else {
			continue;
		}
		score += penalty;
		if (score < bestScore) {
			bestScore = score;
			best = synced;
			bestFactor = factor;
		}
	}
	return best.isEmpty() ? best : ScaleLrc(best, bestFactor);
}

// Three searches, from the most exact to the widest.
void Search(const TrackInfo &info, int generation, int step, bool reached) {
	auto &state = Current();
	const auto clean = CleanTitle(info.title);
	auto url = QUrl(u"https://lrclib.net/api/search"_q);
	auto query = QUrlQuery();
	if (step == 0) {
		query.addQueryItem(u"track_name"_q, clean);
		if (!info.performer.isEmpty()) {
			query.addQueryItem(u"artist_name"_q, info.performer);
		}
	} else if (step == 1) {
		query.addQueryItem(u"q"_q, (info.performer + ' ' + clean).trimmed());
	} else {
		query.addQueryItem(u"q"_q, clean);
	}
	url.setQuery(query);
	const auto reply = state.network->get(MakeRequest(url));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (Current().generation != generation) {
			return;
		}
		auto answered = reached;
		auto lrc = QString();
		if (reply->error() == QNetworkReply::NoError) {
			answered = true;
			lrc = PickBest(QJsonDocument::fromJson(reply->readAll()).array(), info);
		}
		if (lrc.isEmpty() && step < 2) {
			Search(info, generation, step + 1, answered);
			return;
		}
		FinishFetch(info, generation, lrc, answered);
	});
}

void FetchOnline(const TrackInfo &info, int generation) {
	auto &state = Current();
	if (!state.network) {
		state.network = std::make_unique<QNetworkAccessManager>();
	}
	if (SpeedOf(info.title) != 0) {
		// The exact lookup needs the original length; go straight to search.
		Search(info, generation, 0, false);
		return;
	}
	auto url = QUrl(u"https://lrclib.net/api/get"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"track_name"_q, CleanTitle(info.title));
	query.addQueryItem(u"artist_name"_q, info.performer);
	if (info.duration > 0) {
		query.addQueryItem(
			u"duration"_q,
			QString::number(qRound(info.duration / 1000.)));
	}
	url.setQuery(query);
	const auto reply = state.network->get(MakeRequest(url));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (Current().generation != generation) {
			return;
		}
		auto reached = false;
		if (reply->error() == QNetworkReply::NoError) {
			reached = true;
			const auto object = QJsonDocument::fromJson(
				reply->readAll()).object();
			const auto synced = object.value(u"syncedLyrics"_q).toString();
			if (!synced.isEmpty()) {
				FinishFetch(info, generation, synced, true);
				return;
			}
		} else if (reply->error() == QNetworkReply::ContentNotFoundError) {
			reached = true;
		}
		Search(info, generation, 0, reached);
	});
}

// ---------------------------------------------------------------- render

struct Particle {
	QPointF position;
	QPointF velocity;
	double life = 1.;
	double size = 2.;
};

struct Word {
	QString text;
	QPointF position;
	QPointF from;
	double rotation = 0.;
	double size = 1.;
	double phase = 0.;
	crl::time start = 0;
	crl::time end = 0;
	crl::time out = 0;
	QImage dim;
	QImage lit;
	bool gone = false;
};

struct LineLayout {
	int index = 0;
	std::vector<Word> words;
};

[[nodiscard]] QFont WordFont(int style, int pixelSize) {
	auto font = (style == 2)
		? QFontDatabase::systemFont(QFontDatabase::FixedFont)
		: QFont(QApplication::font());
	font.setPixelSize(std::max(pixelSize, 8));
	font.setWeight((style == 2) ? QFont::Bold : QFont::Black);
	if (style == 2) {
		font.setStyleStrategy(QFont::NoAntialias);
	}
	return font;
}

[[nodiscard]] QImage Blur(const QImage &source, int radius) {
	if (source.isNull() || radius <= 0) {
		return source;
	}
	const auto small = source.scaled(
		std::max(source.width() / radius, 1),
		std::max(source.height() / radius, 1),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	return small.scaled(
		source.size(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

[[nodiscard]] QImage RenderWord(
		const QString &text,
		const QFont &font,
		int style,
		bool lit,
		const QColor &accent) {
	const auto ratio = style::DevicePixelRatio();
	const auto metrics = QFontMetrics(font);
	const auto pad = metrics.height() / 2;
	const auto width = metrics.horizontalAdvance(text) + pad * 2;
	const auto height = metrics.height() + pad * 2;
	auto result = QImage(
		QSize(width, height) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.setFont(font);
	if (style != 2) {
		p.setRenderHint(QPainter::TextAntialiasing);
	}
	const auto base = QPoint(pad, pad + metrics.ascent());
	if (style == 1) {
		// 3D: extrusion behind the face.
		auto side = accent.darker(lit ? 220 : 320);
		side.setAlpha(lit ? 230 : 120);
		p.setPen(side);
		const auto depth = std::max(2, metrics.height() / 10);
		for (auto d = depth; d > 0; --d) {
			p.drawText(base + QPoint(d, d), text);
		}
	}
	if (lit && style != 2) {
		auto glow = QImage(result.size(), QImage::Format_ARGB32_Premultiplied);
		glow.setDevicePixelRatio(ratio);
		glow.fill(Qt::transparent);
		{
			auto q = QPainter(&glow);
			q.setFont(font);
			q.setPen(accent);
			q.drawText(base, text);
		}
		glow = Blur(glow, std::max(3, int(4 * ratio)));
		p.setCompositionMode(QPainter::CompositionMode_Plus);
		p.drawImage(0, 0, glow);
		p.drawImage(0, 0, glow);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	}
	p.setPen(lit
		? QColor(255, 225, 230)
		: QColor(255, 233, 236, 80));
	p.drawText(base, text);
	return result;
}

class LyricsWidget final : public Ui::RpWidget {
public:
	explicit LyricsWidget(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void refresh();
	void onState(const ::Media::Player::TrackState &state);
	void tick();
	void wake();
	[[nodiscard]] crl::time position() const;
	[[nodiscard]] bool hideMode() const;
	void ensureLayouts(crl::time now);
	[[nodiscard]] LineLayout layoutLine(int index) const;
	void shatter(const Word &word, QPointF at);
	void paintWord(QPainter &p, const Word &word, crl::time now);

	// "Hide chats" mode: the song cover sways to the music and the lyrics
	// play under it or beside it; the side changes every few lines.
	[[nodiscard]] int coverModeFor(int index) const;
	[[nodiscard]] QRectF coverTarget(int mode) const;
	[[nodiscard]] QRectF lyricsArea(int index) const;
	void paintCover(QPainter &p, crl::time now);
	void resetCover();

	base::Timer _timer;
	std::vector<LineLayout> _layouts;
	std::vector<Particle> _particles;
	DocumentData *_document = nullptr;
	FullMsgId _contextId;
	std::shared_ptr<Data::DocumentMedia> _coverMedia;
	QImage _cover;
	QRectF _coverRect;
	int _coverMode = -1;
	crl::time _coverFrame = 0;
	float64 _coverBeat = 0.;
	crl::time _statePosition = 0;
	crl::time _stateAt = 0;
	bool _playing = false;
	bool _enabled = false;
	int _layoutGeneration = -1;
	crl::time _lastTick = 0;

};

LyricsWidget::LyricsWidget(QWidget *parent)
: RpWidget(parent)
, _timer([=] { tick(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);

	rpl::merge(
		OptionChanges(kOptionLyrics),
		OptionChanges(kOptionLyricsLayout),
		OptionChanges(kOptionLyricsVanish),
		OptionChanges(kOptionLyricsStyle),
		OptionChanges(kOptionLyricsSize),
		OptionChanges(kOptionHideChats),
		OptionChanges(kOptionPalette),
		ConfigChanges(),
		Current().changes.events()
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());

	::Media::Player::instance()->updatedNotifier(
	) | rpl::filter([](const ::Media::Player::TrackState &state) {
		return state.id.type() == AudioMsgId::Type::Song;
	}) | rpl::on_next([=](const ::Media::Player::TrackState &state) {
		onState(state);
	}, lifetime());

	::Media::Player::instance()->stops(
		AudioMsgId::Type::Song
	) | rpl::on_next([=] {
		_playing = false;
		_document = nullptr;
		resetCover();
		Load(nullptr);
	}, lifetime());

	base::install_event_filter(this, QCoreApplication::instance(), [=](
			not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress && hideMode()) {
			const auto key = static_cast<QKeyEvent*>(e.get());
			if (key->key() == Qt::Key_Escape) {
				ToggleHideChats();
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	});

	refresh();
}

bool LyricsWidget::hideMode() const {
	return _enabled
		&& _document
		&& OptionValue(kOptionHideChats)
		&& !Current().lines.empty();
}

void LyricsWidget::refresh() {
	_enabled = EffectsEnabled() && OptionValue(kOptionLyrics);
	_layouts.clear();
	_layoutGeneration = -1;
	const auto hide = hideMode();
	setAttribute(Qt::WA_TransparentForMouseEvents, !hide);
	setVisible(_enabled && _document && !Current().lines.empty());
	if (isVisible()) {
		raise();
	}
	wake();
	update();
}

void LyricsWidget::onState(const ::Media::Player::TrackState &state) {
	const auto document = state.id.audio();
	if (document != _document) {
		_document = document;
		_contextId = state.id.contextId();
		resetCover();
		Load(document);
	}
	const auto frequency = std::max(state.frequency, 1);
	_statePosition = state.position * 1000 / frequency;
	_stateAt = crl::now();
	const auto playing = ::Media::Player::ShowPauseIcon(state.state);
	if (_playing != playing) {
		_playing = playing;
		refresh();
	} else if (_playing) {
		wake();
	}
}

crl::time LyricsWidget::position() const {
	const auto passed = _playing ? (crl::now() - _stateAt) : 0;
	return _statePosition + std::min(passed, crl::time(2000))
		+ Current().offset;
}

void LyricsWidget::wake() {
	const auto needed = isVisible()
		&& ((_playing && _enabled) || !_particles.empty());
	if (!needed) {
		_timer.cancel();
		return;
	}
	if (!_timer.isActive()) {
		_lastTick = crl::now();
		const auto delay = std::clamp(
			crl::time(CurrentQuality().frameDelay),
			crl::time(16),
			crl::time(40));
		_timer.callEach(delay);
	}
}

void LyricsWidget::tick() {
	const auto now = crl::now();
	const auto dt = std::min((now - _lastTick) / 1000., 0.1);
	_lastTick = now;
	const auto window = this->window();
	if (!window
		|| !window->isVisible()
		|| (window->windowState() & Qt::WindowMinimized)) {
		_timer.cancel();
		return;
	}
	for (auto &particle : _particles) {
		particle.life -= dt / 1.1;
		particle.velocity.ry() += 260. * dt;
		particle.velocity *= std::exp(-dt * 0.8);
		particle.position += particle.velocity * dt;
	}
	_particles.erase(
		std::remove_if(_particles.begin(), _particles.end(), [](const Particle &p) {
			return p.life <= 0.;
		}),
		_particles.end());
	update();
	if (!_playing && _particles.empty()) {
		_timer.cancel();
	}
}

LineLayout LyricsWidget::layoutLine(int index) const {
	const auto &state = Current();
	const auto &line = state.lines[index];
	const auto scale = style::Scale() / 100.;
	const auto size = std::clamp(OptionInt(kOptionLyricsSize), 60, 140) / 100.;
	const auto layout = std::clamp(OptionInt(kOptionLyricsLayout), 0, kLayoutCount - 1);
	const auto style = std::clamp(OptionInt(kOptionLyricsStyle), 0, kStyleCount - 1);
	const auto accent = PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;
	auto random = std::mt19937(uint32(qHash(state.track.key) + index * 7919));
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	const auto r = [&] { return unit(random); };

	auto result = LineLayout{ .index = index };
	const auto texts = line.text.split(' ', Qt::SkipEmptyParts);
	if (texts.isEmpty()) {
		return result;
	}
	const auto area = lyricsArea(index);
	const auto base = std::clamp(area.width() / 15., 22. * scale, 60. * scale) * size;

	// Timing: enhanced LRC word times or the line split by word length.
	auto total = 0;
	for (const auto &text : texts) {
		total += text.size() + 1;
	}
	const auto span = std::max(line.end - line.at, crl::time(400)) * 0.8;
	auto at = double(line.at);
	for (auto i = 0; i != texts.size(); ++i) {
		auto word = Word();
		word.text = texts[i];
		if (line.wordTimes.size() == size_t(texts.size())) {
			word.start = line.wordTimes[i];
			word.end = (i + 1 < texts.size())
				? line.wordTimes[i + 1]
				: line.end;
		} else {
			word.start = crl::time(at);
			at += span * (texts[i].size() + 1) / total;
			word.end = crl::time(at);
		}
		word.out = line.end + ((layout == 2)
			? 0
			: crl::time(350. * i / texts.size()));
		word.phase = r() * 6.28;
		const auto angle = r() * 6.28;
		word.from = QPointF(std::cos(angle) * base * 3, std::sin(angle) * base * 2);
		result.words.push_back(std::move(word));
	}

	const auto w = area.width();
	const auto h = area.height();
	const auto center = area.topLeft() + ((layout == 2)
		? QPointF(w / 2., h * 0.42)
		: QPointF(w * (0.5 + (r() - 0.5) * 0.25), h * (0.40 + (r() - 0.5) * 0.18)));

	const auto measure = [&](Word &word, double pixelSize) {
		word.size = pixelSize;
		return QFontMetrics(WordFont(style, int(pixelSize))).horizontalAdvance(word.text);
	};
	if (layout == 2) {
		auto widths = std::vector<double>();
		auto full = 0.;
		const auto space = QFontMetrics(WordFont(style, int(base))).horizontalAdvance(' ');
		for (auto &word : result.words) {
			widths.push_back(measure(word, base));
			full += widths.back() + space;
		}
		auto x = center.x() - full / 2.;
		for (auto i = 0; i != result.words.size(); ++i) {
			result.words[i].position = QPointF(x + widths[i] / 2., center.y());
			x += widths[i] + space;
		}
	} else if (layout == 1) {
		auto x = 0.;
		auto y = 0.;
		auto right = 0.;
		for (auto &word : result.words) {
			const auto ww = measure(word, base * (0.85 + r() * 0.35));
			word.position = QPointF(x + ww / 2., y);
			word.rotation = (r() - 0.5) * 0.08;
			right = std::max(right, x + ww);
			x += ww * 0.55 + base * 0.5;
			y += base * 0.62;
		}
		for (auto &word : result.words) {
			word.position += QPointF(center.x() - right / 2., center.y() - y / 2.);
		}
	} else {
		auto rows = std::vector<std::vector<int>>();
		auto row = std::vector<int>();
		for (auto i = 0; i != result.words.size(); ++i) {
			row.push_back(i);
			if (row.size() >= size_t(1 + int(r() * 3))) {
				rows.push_back(std::move(row));
				row = {};
			}
		}
		if (!row.empty()) {
			rows.push_back(std::move(row));
		}
		const auto rowHeight = base * 1.25;
		for (auto ri = 0; ri != rows.size(); ++ri) {
			auto widths = std::vector<double>();
			auto total = 0.;
			for (const auto i : rows[ri]) {
				widths.push_back(measure(result.words[i], base * (0.8 + r() * 0.5)));
				total += widths.back();
			}
			const auto gap = base * 0.45;
			total += gap * (rows[ri].size() - 1);
			auto x = -total / 2. + (r() - 0.5) * base * 2.;
			for (auto k = 0; k != rows[ri].size(); ++k) {
				auto &word = result.words[rows[ri][k]];
				word.position = QPointF(
					center.x() + x + widths[k] / 2.,
					center.y()
						+ (ri - (rows.size() - 1) / 2.) * rowHeight
						+ (r() - 0.5) * base * 0.5);
				word.rotation = (r() - 0.5) * 0.22;
				x += widths[k] + gap;
			}
		}
	}
	for (auto &word : result.words) {
		const auto font = WordFont(style, int(word.size));
		word.dim = RenderWord(word.text, font, style, false, accent);
		word.lit = RenderWord(word.text, font, style, true, accent);
	}
	return result;
}

void LyricsWidget::ensureLayouts(crl::time now) {
	const auto &state = Current();
	if (_layoutGeneration != state.generation) {
		_layoutGeneration = state.generation;
		_layouts.clear();
	}
	const auto &lines = state.lines;
	for (auto i = 0; i != int(lines.size()); ++i) {
		const auto &line = lines[i];
		if (line.text.trimmed().isEmpty()) {
			continue;
		}
		const auto visible = (now >= line.at - kAppearAhead)
			&& (now < line.end + kVanish + 500);
		const auto existing = std::find_if(
			_layouts.begin(),
			_layouts.end(),
			[&](const LineLayout &layout) { return layout.index == i; });
		if (visible && existing == _layouts.end()) {
			_layouts.push_back(layoutLine(i));
		} else if (!visible && existing != _layouts.end()) {
			_layouts.erase(existing);
		}
	}
}

void LyricsWidget::shatter(const Word &word, QPointF at) {
	if (_particles.size() >= size_t(kMaxParticles)) {
		return;
	}
	const auto &image = word.lit;
	const auto ratio = image.devicePixelRatio();
	const auto step = std::max(3, int(3 * ratio));
	const auto w = image.width();
	const auto h = image.height();
	const auto c = std::cos(word.rotation);
	const auto s = std::sin(word.rotation);
	static auto random = std::mt19937(4242);
	auto unit = std::uniform_real_distribution<double>(0., 1.);
	for (auto y = 0; y < h; y += step) {
		const auto line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (auto x = 0; x < w; x += step) {
			if (qAlpha(line[x]) < 150 || unit(random) > 0.55) {
				continue;
			}
			const auto lx = (x - w / 2.) / ratio;
			const auto ly = (y - h / 2.) / ratio;
			_particles.push_back({
				.position = at + QPointF(lx * c - ly * s, lx * s + ly * c),
				.velocity = QPointF(
					(unit(random) - 0.5) * 180.,
					-unit(random) * 130. - 20.),
				.life = 0.7 + 0.3 * unit(random),
				.size = (1.6 + 1.4 * unit(random)) * style::Scale() / 100.,
			});
			if (_particles.size() >= size_t(kMaxParticles)) {
				return;
			}
		}
	}
}

void LyricsWidget::paintWord(QPainter &p, const Word &word, crl::time now) {
	const auto motion = std::clamp(OptionInt(kOptionLyricsMotion), 0, 100) / 100.;
	const auto vanish = std::clamp(OptionInt(kOptionLyricsVanish), 0, kVanishCount - 1);
	const auto seconds = now / 1000.;
	const auto appear = word.start - kAppearAhead;
	const auto k = std::clamp((now - appear) / double(kFlyIn), 0., 1.);
	const auto ease = 1. - std::pow(1. - k, 3.);
	const auto onScreen = (now - word.start) / 1000.;
	auto position = word.position
		+ word.from * (1. - ease)
		+ QPointF(
			std::sin(seconds * 0.9 + word.phase) * 6. * motion
				+ onScreen * 3. * motion * std::cos(word.phase),
			std::cos(seconds * 1.1 + word.phase) * 5. * motion
				- onScreen * 2. * motion);
	auto opacity = ease;
	auto scale = 0.3 + 0.7 * ease;
	if (now >= word.out) {
		const auto v = std::clamp((now - word.out) / double(kVanish), 0., 1.);
		opacity *= 1. - v;
		if (vanish == 1) {
			scale *= 1. + 0.18 * v;
		} else if (vanish == 2) {
			position += QPointF(std::cos(word.phase) * v * 30., -v * v * 120.);
		}
	}
	if (opacity <= 0.01) {
		return;
	}
	const auto lit = std::clamp(
		(now - word.start) / double(std::max(word.end - word.start, crl::time(50))),
		0.,
		1.);
	const auto ratio = word.dim.devicePixelRatio();
	const auto size = QSizeF(word.dim.size()) / ratio;
	p.save();
	p.translate(position);
	p.rotate(word.rotation * 180. / 3.14159265358979323846);
	p.scale(scale, scale);
	p.setOpacity(opacity);
	const auto origin = QPointF(-size.width() / 2., -size.height() / 2.);
	p.drawImage(origin, word.dim);
	if (lit > 0.) {
		const auto pad = size.height() / 4.;
		const auto visible = pad + (size.width() - pad * 2.) * lit;
		p.setClipRect(QRectF(origin, QSizeF(lit >= 1. ? size.width() : visible, size.height())));
		p.drawImage(origin, word.lit);
	}
	p.restore();
}

void LyricsWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (hideMode()) {
		if (!PaintRoomInWindow(p, this)) {
			const auto accent = PaletteAt(
				std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
				0.).first;
			auto gradient = QRadialGradient(
				QPointF(width() / 2., height() * 0.45),
				std::max(width(), height()) * 0.8);
			gradient.setColorAt(0., accent.darker(420));
			gradient.setColorAt(1., QColor(7, 1, 2));
			p.fillRect(rect(), gradient);
		}
		p.setPen(QColor(255, 255, 255, 90));
		p.drawText(
			rect().adjusted(0, 0, 0, -16),
			Qt::AlignHCenter | Qt::AlignBottom,
			QString::fromUtf8("Esc или клик — вернуть чаты"));
		if (_enabled) {
			paintCover(p, position());
		}
	}
	if (!_enabled) {
		return;
	}
	const auto now = position();
	ensureLayouts(now);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	for (auto &layout : _layouts) {
		for (auto &word : layout.words) {
			if (word.gone || now < word.start - kAppearAhead) {
				continue;
			}
			const auto vanish = OptionInt(kOptionLyricsVanish);
			if (vanish == 0 && now >= word.out) {
				shatter(word, word.position);
				word.gone = true;
				wake();
				continue;
			}
			if (now >= word.out + kVanish) {
				word.gone = true;
				continue;
			}
			paintWord(p, word, now);
		}
	}
	if (!_particles.empty()) {
		p.setPen(Qt::NoPen);
		for (const auto &particle : _particles) {
			p.setOpacity(std::clamp(particle.life, 0., 1.));
			p.fillRect(
				QRectF(particle.position, QSizeF(particle.size, particle.size)),
				QColor(255, int(60 + 120 * particle.life), int(70 + 80 * particle.life)));
		}
		p.setOpacity(1.);
	}
}

void LyricsWidget::resetCover() {
	_coverMedia = nullptr;
	_cover = QImage();
	_coverMode = -1;
	_coverRect = QRectF();
}

int LyricsWidget::coverModeFor(int index) const {
	// 0: cover on top, text below; 1: cover left; 2: cover right.
	// The side changes every four lines so the eye can follow it.
	const auto &state = Current();
	const auto seed = uint32(qHash(state.track.key)) + uint32(index / 4) * 2654435761u;
	return int(seed % 3u);
}

QRectF LyricsWidget::coverTarget(int mode) const {
	const auto w = double(width());
	const auto h = double(height());
	const auto side = std::min(w, h) * (mode == 0 ? 0.30 : 0.38);
	const auto center = (mode == 0)
		? QPointF(w / 2., h * 0.27)
		: (mode == 1)
		? QPointF(w * 0.24, h * 0.46)
		: QPointF(w * 0.76, h * 0.46);
	return QRectF(center - QPointF(side / 2., side / 2.), QSizeF(side, side));
}

QRectF LyricsWidget::lyricsArea(int index) const {
	const auto full = QRectF(rect());
	if (!hideMode()) {
		return full;
	}
	const auto w = full.width();
	const auto h = full.height();
	switch (coverModeFor(index)) {
	case 0: return QRectF(w * 0.06, h * 0.50, w * 0.88, h * 0.42);
	case 1: return QRectF(w * 0.44, h * 0.08, w * 0.53, h * 0.84);
	default: return QRectF(w * 0.03, h * 0.08, w * 0.53, h * 0.84);
	}
}

void LyricsWidget::paintCover(QPainter &p, crl::time now) {
	if (!_document) {
		return;
	}
	// Load the cover (the song thumbnail) once.
	if (!_coverMedia && _document->hasThumbnail()) {
		_coverMedia = _document->createMediaView();
		_coverMedia->thumbnailWanted(Data::FileOrigin(_contextId));
	}
	if (_cover.isNull() && _coverMedia) {
		if (const auto image = _coverMedia->thumbnail()) {
			_cover = image->original();
		}
	}

	// Which side: the line being sung now.
	const auto &lines = Current().lines;
	auto current = 0;
	for (auto i = 0; i != int(lines.size()); ++i) {
		if (lines[i].at <= now) {
			current = i;
		}
	}
	const auto mode = coverModeFor(current);
	const auto target = coverTarget(mode);
	const auto frame = crl::now();
	const auto dt = _coverFrame ? std::min((frame - _coverFrame) / 1000., 0.1) : 1.;
	_coverFrame = frame;
	if (_coverRect.isEmpty() || _coverMode < 0) {
		_coverRect = target;
	} else {
		const auto k = std::min(1., dt * 3.);
		_coverRect = QRectF(
			_coverRect.topLeft() + (target.topLeft() - _coverRect.topLeft()) * k,
			_coverRect.size() + (target.size() - _coverRect.size()) * k);
	}
	_coverMode = mode;

	// Sway and a soft pulse on the beat.
	const auto mixer = ::Media::Player::mixer();
	const auto level = (_playing && mixer) ? mixer->songLevel() : 0.;
	_coverBeat = std::max(level, _coverBeat - dt * 2.5);
	const auto t = frame / 1000.;
	const auto angle = std::sin(t * 0.8) * 5.;
	const auto bob = std::sin(t * 1.1) * _coverRect.height() * 0.025;
	const auto zoom = 1. + 0.05 * _coverBeat;
	const auto accent = PaletteAt(
		std::clamp(OptionInt(kOptionPalette), 0, PalettesCount() - 1),
		0.).first;

	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.translate(_coverRect.center() + QPointF(0., bob));
	p.rotate(angle);
	p.scale(zoom, zoom);
	const auto half = _coverRect.width() / 2.;
	const auto box = QRectF(-half, -half, half * 2., half * 2.);
	const auto radius = half * 0.12;

	// Glow behind the cover.
	for (auto i = 4; i > 0; --i) {
		auto glow = accent;
		glow.setAlpha(int((18 + 40 * _coverBeat) / i));
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		const auto grow = half * 0.05 * i;
		p.drawRoundedRect(box.adjusted(-grow, -grow, grow, grow), radius + grow, radius + grow);
	}
	auto clip = QPainterPath();
	clip.addRoundedRect(box, radius, radius);
	p.setClipPath(clip);
	if (!_cover.isNull()) {
		const auto source = _cover.size();
		const auto side = std::min(source.width(), source.height());
		const auto crop = QRect(
			(source.width() - side) / 2,
			(source.height() - side) / 2,
			side,
			side);
		p.drawImage(box, _cover, crop);
	} else {
		// No cover in the file: a dark disc with a note.
		auto gradient = QLinearGradient(box.topLeft(), box.bottomRight());
		gradient.setColorAt(0., accent.darker(160));
		gradient.setColorAt(1., QColor(12, 2, 4));
		p.fillRect(box, gradient);
		p.setPen(QColor(255, 255, 255, 200));
		auto font = p.font();
		font.setPixelSize(int(half * 0.9));
		p.setFont(font);
		p.drawText(box, Qt::AlignCenter, QString::fromUtf8("♪"));
	}
	p.setClipping(false);
	p.setPen(QPen(QColor(255, 255, 255, 60), 1.5));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(box, radius, radius);
	p.restore();
}

void LyricsWidget::mousePressEvent(QMouseEvent *e) {
	if (hideMode()) {
		ToggleHideChats();
	}
}

} // namespace

std::vector<Line> ParseLrc(const QString &text) {
	auto result = std::vector<Line>();
	static const auto tag = QRegularExpression(
		u"\\[(\\d{1,3}):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]"_q);
	static const auto offsetTag = QRegularExpression(
		u"\\[offset:\\s*([+-]?\\d+)\\s*\\]"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto wordTag = QRegularExpression(
		u"<(\\d{1,3}):(\\d{1,2})(?:[.:](\\d{1,3}))?>"_q);
	const auto toMs = [](const QRegularExpressionMatch &m) {
		const auto minutes = m.captured(1).toLongLong();
		const auto seconds = m.captured(2).toLongLong();
		auto fraction = m.captured(3);
		auto ms = 0LL;
		if (!fraction.isEmpty()) {
			while (fraction.size() < 3) {
				fraction += '0';
			}
			ms = fraction.left(3).toLongLong();
		}
		return crl::time((minutes * 60 + seconds) * 1000 + ms);
	};
	auto offset = crl::time(0);
	for (const auto &raw : text.split('\n')) {
		const auto line = raw.trimmed();
		if (const auto m = offsetTag.match(line); m.hasMatch()) {
			offset = m.captured(1).toLongLong();
			continue;
		}
		auto times = std::vector<crl::time>();
		auto rest = line;
		while (true) {
			const auto m = tag.match(rest);
			if (!m.hasMatch() || m.capturedStart() != 0) {
				break;
			}
			times.push_back(toMs(m));
			rest = rest.mid(m.capturedLength());
		}
		if (times.empty()) {
			continue;
		}
		auto words = std::vector<crl::time>();
		auto clean = QString();
		auto it = wordTag.globalMatch(rest);
		if (it.hasNext()) {
			auto last = 0;
			auto pending = std::optional<crl::time>();
			while (it.hasNext()) {
				const auto m = it.next();
				const auto chunk = rest.mid(last, m.capturedStart() - last);
				if (pending && !chunk.trimmed().isEmpty()) {
					for (const auto &w : chunk.split(' ', Qt::SkipEmptyParts)) {
						Q_UNUSED(w);
						words.push_back(*pending);
					}
				}
				clean += chunk;
				pending = toMs(m);
				last = m.capturedEnd();
			}
			const auto tail = rest.mid(last);
			if (pending && !tail.trimmed().isEmpty()) {
				for (const auto &w : tail.split(' ', Qt::SkipEmptyParts)) {
					Q_UNUSED(w);
					words.push_back(*pending);
				}
			}
			clean += tail;
		} else {
			clean = rest;
		}
		clean = clean.simplified();
		for (const auto at : times) {
			result.push_back({
				.at = std::max(at - offset, crl::time(0)),
				.text = clean,
				.wordTimes = words,
			});
		}
	}
	std::sort(result.begin(), result.end(), [](const Line &a, const Line &b) {
		return a.at < b.at;
	});
	for (auto i = 0; i != int(result.size()); ++i) {
		const auto next = (i + 1 < int(result.size()))
			? result[i + 1].at
			: result[i].at + 5000;
		result[i].end = std::max(
			result[i].at + 300,
			std::min(next - 150, result[i].at + 9000));
		if (!result[i].wordTimes.empty()
			&& result[i].wordTimes.size()
				!= size_t(result[i].text.split(' ', Qt::SkipEmptyParts).size())) {
			result[i].wordTimes.clear();
		}
	}
	return result;
}

QString SerializeLrc(const std::vector<Line> &lines) {
	auto result = QString();
	for (const auto &line : lines) {
		const auto minutes = line.at / 60000;
		const auto seconds = (line.at / 1000) % 60;
		const auto hundredths = (line.at % 1000) / 10;
		result += u"[%1:%2.%3]%4\n"_q
			.arg(minutes, 2, 10, QChar('0'))
			.arg(seconds, 2, 10, QChar('0'))
			.arg(hundredths, 2, 10, QChar('0'))
			.arg(line.text);
	}
	return result;
}

QString LayoutName(int index) {
	switch (index) {
	case 1: return QString::fromUtf8("Лесенка");
	case 2: return QString::fromUtf8("Строкой");
	}
	return QString::fromUtf8("По словам");
}

QString VanishName(int index) {
	switch (index) {
	case 1: return QString::fromUtf8("Растворение");
	case 2: return QString::fromUtf8("Улетает вверх");
	}
	return QString::fromUtf8("Искры");
}

QString StyleName(int index) {
	switch (index) {
	case 1: return QString::fromUtf8("3D");
	case 2: return QString::fromUtf8("Пиксели");
	}
	return QString::fromUtf8("Неон");
}

void Setup(not_null<Ui::RpWidget*> body) {
	const auto widget = Ui::CreateChild<LyricsWidget>(body.get());
	body->sizeValue(
	) | rpl::on_next([=](QSize size) {
		widget->setGeometry(QRect(QPoint(), size));
		widget->raise();
	}, widget->lifetime());
}

bool HideChatsEnabled() {
	return OptionValue(kOptionHideChats);
}

void ToggleHideChats() {
	base::options::lookup<bool>(kOptionHideChats).set(!HideChatsEnabled());
}

void ShowOffsetBox(not_null<Window::SessionController*> controller) {
	const auto &state = Current();
	if (state.track.key.isEmpty()) {
		return;
	}
	const auto key = state.track.key;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Сдвиг текста песни")));
		const auto value = std::make_shared<rpl::variable<int>>(
			int(Current().offset));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			value->value() | rpl::map([](int ms) {
				return QString::fromUtf8("Текст идёт %1 на %2 с")
					.arg(ms >= 0
						? QString::fromUtf8("раньше")
						: QString::fromUtf8("позже"))
					.arg(QString::number(std::abs(ms) / 1000., 'f', 1));
			}),
			st::boxLabel));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8(
				"Нужен, если у трека вступление, которого нет в тексте.")),
			st::boxDividerLabel));
		const auto apply = [=](int ms) {
			value->force_assign(ms);
			auto offsets = ReadOffsets();
			if (ms) {
				offsets.insert(key, ms);
			} else {
				offsets.remove(key);
			}
			WriteOffsets(offsets);
			if (Current().track.key == key) {
				Current().offset = ms;
			}
		};
		box->addButton(rpl::single(QString::fromUtf8("−0.5 с")), [=] {
			apply(value->current() - 500);
		});
		box->addButton(rpl::single(QString::fromUtf8("+0.5 с")), [=] {
			apply(value->current() + 500);
		});
		box->addLeftButton(rpl::single(QString::fromUtf8("Сброс")), [=] {
			apply(0);
		});
	}));
}

namespace {

[[nodiscard]] QString FormatTime(crl::time ms) {
	ms = std::max(ms, crl::time(0));
	return u"%1:%2.%3"_q
		.arg(ms / 60000)
		.arg((ms / 1000) % 60, 2, 10, QChar('0'))
		.arg((ms % 1000) / 10, 2, 10, QChar('0'));
}

// Lines as the user edits them: "0:12.50 text", word times as "<0:13>word".
[[nodiscard]] QString HumanFromLines(const std::vector<Line> &lines) {
	auto result = QString();
	for (const auto &line : lines) {
		auto text = line.text;
		const auto words = line.text.split(' ', Qt::SkipEmptyParts);
		if (!line.wordTimes.empty()
			&& line.wordTimes.size() == size_t(words.size())) {
			text.clear();
			for (auto i = 0; i != words.size(); ++i) {
				if (i) {
					text += ' ' + ('<' + FormatTime(line.wordTimes[i]) + '>');
				}
				text += words[i];
			}
		}
		result += FormatTime(line.at) + ' ' + text + '\n';
	}
	return result;
}

struct HumanParse {
	QString lrc;
	int lines = 0;
	int bad = 0;
};

[[nodiscard]] HumanParse LrcFromHuman(const QString &text) {
	static const auto head = QRegularExpression(
		uR"(^\[?(\d{1,3}):(\d{1,2})(?:[.,](\d{1,3}))?\]?\s*(.*)$)"_q);
	static const auto comma = QRegularExpression(
		uR"(<(\d{1,3}):(\d{1,2}),(\d{1,3})>)"_q);
	auto result = HumanParse();
	for (const auto &raw : text.split('\n')) {
		const auto line = raw.trimmed();
		if (line.isEmpty()) {
			continue;
		}
		const auto m = head.match(line);
		if (!m.hasMatch() || m.captured(4).trimmed().isEmpty()) {
			++result.bad;
			continue;
		}
		const auto time = m.captured(1)
			+ ':'
			+ m.captured(2).rightJustified(2, '0')
			+ (m.captured(3).isEmpty() ? QString() : ('.' + m.captured(3)));
		auto rest = m.captured(4).trimmed();
		rest.replace(comma, u"<\\1:\\2.\\3>"_q);
		if (rest.contains('<') && !rest.startsWith('<')) {
			// The first word starts with the line itself.
			rest = '<' + time + '>' + rest;
		}
		result.lrc += '[' + time + ']' + rest + '\n';
		++result.lines;
	}
	return result;
}

[[nodiscard]] crl::time PlayingPosition() {
	const auto playing = ::Media::Player::instance()->getState(
		AudioMsgId::Type::Song);
	const auto frequency = std::max(playing.frequency, 1);
	return crl::time(playing.position * 1000 / frequency);
}

} // namespace

void ShowSyncEditor(not_null<Window::SessionController*> controller) {
	const auto &state = Current();
	if (!state.track.document) {
		return;
	}
	const auto track = state.track;
	const auto existing = HumanFromLines(state.lines);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Текст песни")));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8(
				"Каждая строчка: время, потом текст.\n"
				"0:12 Пообещай мне\n"
				"0:15.5 Что ты будешь рядом\n"
				"Время отдельного слова: 0:20 Я <0:21>тебя <0:22>жду\n"
				"«Время сейчас» вставляет текущую секунду трека туда, где курсор.")),
			st::boxLabel));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			rpl::single(QString::fromUtf8("0:12 Первая строчка")),
			existing));
		field->setMaxLength(20000);
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::boxDividerLabel));
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(rpl::single(QString::fromUtf8("Сохранить")), [=] {
			const auto parsed = LrcFromHuman(field->getLastText());
			if (!parsed.lines) {
				status->setText(QString::fromUtf8(
					"Нет ни одной строчки со временем. Пример: 0:12 Текст"));
				field->showError();
				return;
			}
			WriteText(CachePath(track, u".lrc"_q), parsed.lrc);
			QFile::remove(CachePath(track, u".none2"_q));
			if (Current().track.key == track.key) {
				Load(track.document);
			}
			box->closeBox();
		});
		box->addButton(rpl::single(QString::fromUtf8("Время сейчас")), [=] {
			field->rawTextEdit()->insertPlainText(
				FormatTime(PlayingPosition()) + ' ');
			field->setFocusFast();
		});
		box->addLeftButton(rpl::single(QString::fromUtf8("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

void FillSongMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller) {
	if (!EffectsEnabled() || !OptionValue(kOptionLyrics)) {
		return;
	}
	menu->addAction(
		(HideChatsEnabled()
			? QString::fromUtf8("✓ Спрятать чаты (текст песни)")
			: QString::fromUtf8("Спрятать чаты (текст песни)")),
		[] { ToggleHideChats(); },
		&st::menuIconShowAll);
	if (Current().track.document) {
		menu->addAction(
			QString::fromUtf8("Сдвиг текста песни…"),
			[=] { ShowOffsetBox(controller); },
			&st::menuIconTimer);
		menu->addAction(
			QString::fromUtf8("Написать / исправить текст…"),
			[=] { ShowSyncEditor(controller); },
			&st::menuIconEdit);
	}
}

} // namespace Mwtk::Lyrics
