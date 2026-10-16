package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.FileLoader;
import org.telegram.messenger.ImageLocation;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.ConnectionsManager;
import org.telegram.tgnet.TLRPC;
import org.telegram.ui.ActionBar.ActionBar;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.ActionBar.Theme;
import org.telegram.ui.Components.BackupImageView;
import org.telegram.ui.Components.LayoutHelper;

import java.util.ArrayList;

/**
 * MWTKgram music: playlists as tiles, a playlist page with covers, search over
 * public Telegram channels, a mini player and VK-style lyrics.
 */
public class MwtkMusicActivity extends BaseFragment {

    private static final int ACCENT = 0xffff2d46;
    private static final int BG = 0xff0b0507;
    private static final int DIM = 0x8cffffff;

    private LinearLayout content;
    private ScrollView scroll;
    private EditText search;
    private FrameLayout root;
    private View player;
    private BackupImageView playerCover;
    private TextView playerTitle;
    private TextView playerArtist;
    private TextView playerPlay;
    private View playerBar;
    private LyricsView lyricsView;
    private int openPlaylist = -1;
    private int searchRequest;
    private final ArrayList<MessageObject> results = new ArrayList<>();
    private String resultsTitle = "Найдено";
    private int searchGeneration;
    private final Runnable tick = this::tick;

    @Override
    public View createView(Context context) {
        actionBar.setBackButtonImage(org.telegram.messenger.R.drawable.ic_ab_back);
        actionBar.setTitle("Музыка");
        actionBar.setActionBarMenuOnItemClick(new ActionBar.ActionBarMenuOnItemClick() {
            @Override
            public void onItemClick(int id) {
                if (id == -1) {
                    if (!onBackPressed(true)) {
                        return;
                    }
                    finishFragment();
                }
            }
        });

        root = new FrameLayout(context);
        root.setBackgroundColor(BG);
        fragmentView = root;

        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        root.addView(column, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));

        search = new EditText(context);
        search.setHint("Поиск песен: исполнитель или название");
        search.setHintTextColor(0x66ffffff);
        search.setTextColor(0xffffffff);
        search.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 15);
        search.setSingleLine(true);
        search.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        search.setPadding(AndroidUtilities.dp(14), AndroidUtilities.dp(10), AndroidUtilities.dp(14), AndroidUtilities.dp(10));
        GradientDrawable field = new GradientDrawable();
        field.setColor(0xb3280a10);
        field.setCornerRadius(AndroidUtilities.dp(14));
        field.setStroke(AndroidUtilities.dp(1), 0x40ff2d46);
        search.setBackground(field);
        search.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_SEARCH
                || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                runSearch(search.getText().toString().trim());
                AndroidUtilities.hideKeyboard(search);
                return true;
            }
            return false;
        });
        search.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                if (s.length() == 0 && !results.isEmpty()) {
                    results.clear();
                    rebuild();
                }
            }
        });
        column.addView(search, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 12, 10, 12, 6));

        scroll = new ScrollView(context);
        scroll.setClipToPadding(false);
        scroll.setPadding(0, 0, 0, AndroidUtilities.dp(96));
        content = new LinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(content, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        column.addView(scroll, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 0, 1f));

        player = createPlayer(context);
        root.addView(player, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, 70, Gravity.BOTTOM, 10, 0, 10, 10));

        lyricsView = new LyricsView(context);
        lyricsView.setVisibility(View.GONE);
        root.addView(lyricsView, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));

        rebuild();
        tick();
        return fragmentView;
    }

    @Override
    public boolean onBackPressed(boolean invoked) {
        if (lyricsView != null && lyricsView.getVisibility() == View.VISIBLE) {
            if (invoked) {
                lyricsView.setVisibility(View.GONE);
            }
            return false;
        }
        if (!results.isEmpty() || (search != null && search.length() > 0)) {
            if (invoked) {
                results.clear();
                search.setText("");
                rebuild();
            }
            return false;
        }
        if (openPlaylist >= 0) {
            if (invoked) {
                openPlaylist = -1;
                rebuild();
            }
            return false;
        }
        return super.onBackPressed(invoked);
    }

    private int coversVersion = MwtkCovers.version();
    private final Runnable coversListener = () -> {
        if (coversVersion != MwtkCovers.version()) {
            coversVersion = MwtkCovers.version();
            rebuild();
        }
    };

    @Override
    public boolean onFragmentCreate() {
        MwtkConfig.addListener(coversListener);
        return super.onFragmentCreate();
    }

    @Override
    public void onFragmentDestroy() {
        MwtkConfig.removeListener(coversListener);
        AndroidUtilities.cancelRunOnUIThread(tick);
        if (searchRequest != 0) {
            ConnectionsManager.getInstance(currentAccount).cancelRequest(searchRequest, true);
        }
        super.onFragmentDestroy();
    }

    // ------------------------------------------------------------ views

    private void rebuild() {
        if (content == null) {
            return;
        }
        content.removeAllViews();
        Context context = getParentActivity();
        if (context == null) {
            return;
        }
        if (!results.isEmpty()) {
            actionBar.setTitle(resultsTitle);
            content.addView(label(context, resultsTitle));
            for (int i = 0; i < results.size(); i++) {
                MessageObject message = results.get(i);
                TrackRow row = new TrackRow(context);
                row.bind(message, message.getMusicTitle(), message.getMusicAuthor(), (int) message.getDuration(), "＋");
                row.setOnClickListener(v -> MwtkPlaylists.playMessages(results, message));
                row.setOnLongClickListener(v -> {
                    MwtkCovers.pick(this, message);
                    return true;
                });
                row.action.setOnClickListener(v -> MwtkMusicBot.saveAndAdd(this, message));
                if ("Мой микс".equals(resultsTitle)) {
                    row.showExtra("👎");
                    row.extra.setOnClickListener(v -> {
                        MwtkTaste.dislike(message.getMusicAuthor(), message.getMusicTitle());
                        MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
                        if (playing != null && playing.getId() == message.getId() && playing.getDialogId() == message.getDialogId()) {
                            MediaController.getInstance().playNextMessage();
                        }
                        results.remove(message);
                        content.removeView(row);
                        MwtkUi.toast("Больше не будет в миксе. Два «👎» одному исполнителю — и он тоже пропадёт");
                    });
                }
                content.addView(row);
            }
            return;
        }
        if (openPlaylist >= 0 && openPlaylist < MwtkPlaylists.count()) {
            showPlaylist(context, openPlaylist);
            return;
        }
        openPlaylist = -1;
        actionBar.setTitle("Музыка");
        TextView mix = new TextView(context);
        mix.setText("▶  Мой микс");
        mix.setTextColor(0xffffffff);
        mix.setTextSize(15);
        mix.setTypeface(AndroidUtilities.bold());
        mix.setGravity(Gravity.CENTER);
        GradientDrawable mixBg = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] { ACCENT, 0xff8a1230 });
        mixBg.setCornerRadius(AndroidUtilities.dp(14));
        mix.setBackground(mixBg);
        mix.setOnClickListener(v -> runMix());
        TextView karaoke = new TextView(context);
        karaoke.setText("🎤  Караоке");
        karaoke.setTextColor(0xffffffff);
        karaoke.setTextSize(15);
        karaoke.setTypeface(AndroidUtilities.bold());
        karaoke.setGravity(Gravity.CENTER);
        GradientDrawable karaokeBg = new GradientDrawable();
        karaokeBg.setColor(0x33ffffff);
        karaokeBg.setCornerRadius(AndroidUtilities.dp(14));
        karaoke.setBackground(karaokeBg);
        karaoke.setOnClickListener(v -> MwtkKaraoke.show(this));
        LinearLayout buttonsRow = new LinearLayout(context);
        buttonsRow.setOrientation(LinearLayout.HORIZONTAL);
        buttonsRow.addView(mix, LayoutHelper.createLinear(0, 48, 1f));
        buttonsRow.addView(karaoke, LayoutHelper.createLinear(0, 48, 1f, 10, 0, 0, 0));
        content.addView(buttonsRow, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 48, 12, 10, 12, 2));
        TextView importLink = new TextView(context);
        importLink.setText("Перенести плейлист по ссылке (Spotify, Яндекс, Deezer)…");
        importLink.setTextColor(ACCENT);
        importLink.setTextSize(14);
        importLink.setPadding(AndroidUtilities.dp(16), AndroidUtilities.dp(12), AndroidUtilities.dp(16), AndroidUtilities.dp(4));
        importLink.setOnClickListener(v -> MwtkUi.input(this, "Ссылка на плейлист",
            "Вставь ссылку на открытый плейлист Spotify, Яндекс Музыки или Deezer.",
            "", "https://…", false, false, null, this::runImport));
        content.addView(importLink);
        content.addView(label(context, "Плейлисты"));
        int count = MwtkPlaylists.count();
        LinearLayout row = null;
        for (int i = 0; i <= count; i++) {
            if (i % 2 == 0) {
                row = new LinearLayout(context);
                row.setOrientation(LinearLayout.HORIZONTAL);
                content.addView(row, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 8, 0, 8, 6));
            }
            View tile = i < count ? playlistTile(context, i) : newTile(context);
            row.addView(tile, LayoutHelper.createLinear(0, LayoutHelper.WRAP_CONTENT, 1f, 4, 0, 4, 0));
        }
        if (count % 2 == 0) {
            row.addView(new View(context), LayoutHelper.createLinear(0, 1, 1f, 4, 0, 4, 0));
        }
        if (count == 0) {
            TextView hint = new TextView(context);
            hint.setTextColor(DIM);
            hint.setTextSize(14);
            hint.setText("Найди песню через поиск сверху и нажми «＋», или зажми музыку в чате → «В плейлист MWTK».");
            content.addView(hint, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 16, 12, 16, 0));
        }
    }

    private TextView label(Context context, String text) {
        TextView view = new TextView(context);
        view.setText(text.toUpperCase());
        view.setTextColor(DIM);
        view.setTextSize(12);
        view.setLetterSpacing(0.08f);
        view.setPadding(AndroidUtilities.dp(16), AndroidUtilities.dp(14), AndroidUtilities.dp(16), AndroidUtilities.dp(8));
        return view;
    }

    private static int[] colorsFor(String seed) {
        int h = Math.abs(seed == null ? 0 : seed.hashCode());
        float hue = h % 360;
        int a = Color.HSVToColor(new float[] { hue, 0.75f, 0.85f });
        int b = Color.HSVToColor(new float[] { (hue + 40) % 360, 0.7f, 0.25f });
        return new int[] { a, b };
    }

    private static Drawable placeholder(String seed) {
        GradientDrawable drawable = new GradientDrawable(GradientDrawable.Orientation.TL_BR, colorsFor(seed));
        drawable.setCornerRadius(AndroidUtilities.dp(10));
        return drawable;
    }

    private static void setCover(BackupImageView view, MessageObject message, String seed) {
        android.graphics.Bitmap custom = MwtkCovers.get(message);
        if (custom != null) {
            view.setImageBitmap(custom);
            return;
        }
        Drawable placeholder = placeholder(seed);
        TLRPC.Document document = message != null ? message.getDocument() : null;
        TLRPC.PhotoSize thumb = document != null ? FileLoader.getClosestPhotoSizeWithSize(document.thumbs, 320) : null;
        if (thumb != null) {
            view.setImage(ImageLocation.getForDocument(thumb, document), "120_120", placeholder, message);
        } else {
            view.setImageDrawable(placeholder);
        }
    }

    private View playlistTile(Context context, int index) {
        LinearLayout tile = new LinearLayout(context);
        tile.setOrientation(LinearLayout.VERTICAL);
        Collage collage = new Collage(context);
        tile.addView(collage, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 150));
        TextView name = new TextView(context);
        name.setText(MwtkPlaylists.name(index));
        name.setTextColor(0xffffffff);
        name.setTextSize(15);
        name.setTypeface(AndroidUtilities.bold());
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.END);
        tile.addView(name, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 2, 8, 2, 0));
        JSONArray tracks = MwtkPlaylists.tracks(index);
        int total = 0;
        for (int i = 0; i < tracks.length(); i++) {
            JSONObject t = tracks.optJSONObject(i);
            total += t != null ? t.optInt("duration") : 0;
        }
        TextView meta = new TextView(context);
        meta.setText(tracks.length() + " " + songs(tracks.length()) + " · " + duration(total));
        meta.setTextColor(DIM);
        meta.setTextSize(12);
        tile.addView(meta, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 2, 2, 2, 8));
        collage.bind(index);
        tile.setOnClickListener(v -> {
            openPlaylist = index;
            rebuild();
            scroll.scrollTo(0, 0);
        });
        tile.setOnLongClickListener(v -> {
            playlistMenu(index);
            return true;
        });
        return tile;
    }

    private View newTile(Context context) {
        LinearLayout tile = new LinearLayout(context);
        tile.setOrientation(LinearLayout.VERTICAL);
        TextView plus = new TextView(context);
        plus.setText("＋");
        plus.setTextSize(40);
        plus.setTextColor(ACCENT);
        plus.setGravity(Gravity.CENTER);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0x14ff2d46);
        bg.setCornerRadius(AndroidUtilities.dp(16));
        bg.setStroke(AndroidUtilities.dp(1), 0x66ff2d46, AndroidUtilities.dp(6), AndroidUtilities.dp(4));
        plus.setBackground(bg);
        tile.addView(plus, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 150));
        TextView name = new TextView(context);
        name.setText("Новый плейлист");
        name.setTextColor(0xffffffff);
        name.setTextSize(15);
        name.setTypeface(AndroidUtilities.bold());
        tile.addView(name, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 2, 8, 2, 8));
        tile.setOnClickListener(v -> MwtkUi.input(this, "Новый плейлист", null, "", "Название", false, false, null, text -> {
            if (!text.trim().isEmpty()) {
                MwtkPlaylists.create(text.trim());
                rebuild();
            }
        }));
        return tile;
    }

    private void playlistMenu(int index) {
        Context context = getParentActivity();
        if (context == null) {
            return;
        }
        org.telegram.ui.ActionBar.AlertDialog.Builder builder = new org.telegram.ui.ActionBar.AlertDialog.Builder(context);
        builder.setTitle(MwtkPlaylists.name(index));
        builder.setItems(new CharSequence[] { "Переименовать", "Удалить" }, (dialog, which) -> {
            if (which == 0) {
                MwtkUi.input(this, "Название", null, MwtkPlaylists.name(index), "Название", false, false, null, text -> {
                    MwtkPlaylists.rename(index, text);
                    rebuild();
                });
            } else {
                MwtkUi.confirm(this, "Удалить плейлист?", MwtkPlaylists.name(index), "Удалить", () -> {
                    MwtkPlaylists.remove(index);
                    openPlaylist = -1;
                    rebuild();
                });
            }
        });
        showDialog(builder.create());
    }

    private void showPlaylist(Context context, int index) {
        actionBar.setTitle(MwtkPlaylists.name(index));
        JSONArray tracks = MwtkPlaylists.tracks(index);
        int total = 0;
        for (int i = 0; i < tracks.length(); i++) {
            JSONObject t = tracks.optJSONObject(i);
            total += t != null ? t.optInt("duration") : 0;
        }

        LinearLayout hero = new LinearLayout(context);
        hero.setOrientation(LinearLayout.HORIZONTAL);
        hero.setGravity(Gravity.BOTTOM);
        Collage collage = new Collage(context);
        collage.bind(index);
        hero.addView(collage, LayoutHelper.createLinear(120, 120));
        LinearLayout info = new LinearLayout(context);
        info.setOrientation(LinearLayout.VERTICAL);
        TextView name = new TextView(context);
        name.setText(MwtkPlaylists.name(index));
        name.setTextColor(0xffffffff);
        name.setTextSize(22);
        name.setTypeface(AndroidUtilities.bold());
        info.addView(name);
        TextView meta = new TextView(context);
        meta.setText(tracks.length() + " " + songs(tracks.length()) + " · " + duration(total));
        meta.setTextColor(DIM);
        meta.setTextSize(13);
        info.addView(meta, LayoutHelper.createLinear(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, 0, 2, 0, 10));
        LinearLayout buttons = new LinearLayout(context);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        TextView play = pill(context, "▶ Слушать", true);
        play.setOnClickListener(v -> MwtkPlaylists.play(index, 0, false));
        buttons.addView(play);
        TextView shuffle = pill(context, "🔀", false);
        shuffle.setOnClickListener(v -> MwtkPlaylists.play(index, 0, true));
        buttons.addView(shuffle, LayoutHelper.createLinear(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, 8, 0, 0, 0));
        TextView more = pill(context, "⋯", false);
        more.setOnClickListener(v -> playlistMenu(index));
        buttons.addView(more, LayoutHelper.createLinear(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, 8, 0, 0, 0));
        info.addView(buttons);
        hero.addView(info, LayoutHelper.createLinear(0, LayoutHelper.WRAP_CONTENT, 1f, 16, 0, 0, 0));
        content.addView(hero, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 16, 8, 16, 14));

        if (tracks.length() == 0) {
            TextView hint = new TextView(context);
            hint.setTextColor(DIM);
            hint.setTextSize(14);
            hint.setText("Плейлист пуст. Найди песню через поиск сверху и нажми «＋».");
            content.addView(hint, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 16, 4, 16, 0));
            return;
        }
        final ArrayList<TrackRow> rows = new ArrayList<>();
        for (int i = 0; i < tracks.length(); i++) {
            JSONObject t = tracks.optJSONObject(i);
            if (t == null) {
                continue;
            }
            final int trackIndex = i;
            TrackRow row = new TrackRow(context);
            MessageObject cached = MwtkPlaylists.cached(t.optLong("peer"), t.optLong("msg"));
            row.bind(cached, t.optString("title"), t.optString("performer"), t.optInt("duration"), "⋯");
            row.setOnClickListener(v -> MwtkPlaylists.play(index, trackIndex, false));
            row.action.setOnClickListener(v -> {
                MessageObject message = row.boundMessage;
                boolean custom = message != null && MwtkCovers.has(message);
                String[] items = custom
                    ? new String[] { "Своя обложка…", "Убрать свою обложку", "Убрать из плейлиста" }
                    : new String[] { "Своя обложка…", "Убрать из плейлиста" };
                org.telegram.ui.ActionBar.AlertDialog.Builder builder = new org.telegram.ui.ActionBar.AlertDialog.Builder(getParentActivity());
                builder.setTitle(t.optString("title"));
                builder.setItems(items, (d, which) -> {
                    String item = items[which];
                    if (item.startsWith("Своя")) {
                        if (message == null) {
                            MwtkUi.toast("Песня ещё загружается, попробуй через секунду");
                        } else {
                            MwtkCovers.pick(this, message);
                        }
                    } else if (item.startsWith("Убрать свою")) {
                        MwtkCovers.remove(message);
                    } else {
                        MwtkPlaylists.removeTrack(index, trackIndex);
                        rebuild();
                    }
                });
                showDialog(builder.create());
            });
            rows.add(row);
            content.addView(row);
        }
        MwtkPlaylists.load(index, messages -> {
            if (openPlaylist != index || content == null) {
                return;
            }
            for (int i = 0; i < rows.size() && i < messages.size(); i++) {
                if (messages.get(i) != null) {
                    rows.get(i).setMessage(messages.get(i));
                }
            }
        });
    }

    private TextView pill(Context context, String text, boolean accent) {
        TextView view = new TextView(context);
        view.setText(text);
        view.setTextColor(0xffffffff);
        view.setTextSize(14);
        view.setTypeface(AndroidUtilities.bold());
        view.setPadding(AndroidUtilities.dp(16), AndroidUtilities.dp(8), AndroidUtilities.dp(16), AndroidUtilities.dp(8));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(accent ? ACCENT : 0x14ffffff);
        bg.setCornerRadius(AndroidUtilities.dp(20));
        view.setBackground(bg);
        return view;
    }

    private static String songs(int count) {
        int n = Math.abs(count) % 100;
        int n1 = n % 10;
        if (n > 10 && n < 20) {
            return "песен";
        } else if (n1 > 1 && n1 < 5) {
            return "песни";
        } else if (n1 == 1) {
            return "песня";
        }
        return "песен";
    }

    private static String duration(int seconds) {
        seconds = Math.max(0, seconds);
        if (seconds >= 3600) {
            return (seconds / 3600) + " ч " + ((seconds / 60) % 60) + " мин";
        }
        return (seconds / 60) + ":" + String.format(java.util.Locale.US, "%02d", seconds % 60);
    }

    // ------------------------------------------------------------ search

    private void showResults(String title, ArrayList<MessageObject> found) {
        resultsTitle = title;
        results.clear();
        results.addAll(found);
        rebuild();
        if (scroll != null) {
            scroll.scrollTo(0, 0);
        }
    }

    private void runMix() {
        int generation = ++searchGeneration;
        MwtkUi.toast("Подбираю похожую музыку…");
        MwtkMusicBot.mix(currentAccount, (songs, hadArtists) -> {
            if (generation != searchGeneration || getParentActivity() == null) {
                return;
            }
            if (songs.isEmpty()) {
                MwtkUi.toast(hadArtists
                    ? "Музыкальный поиск Telegram не ответил, попробуй ещё раз"
                    : "Добавь песни в плейлисты или включи песню — микс подберётся по ним");
                return;
            }
            showResults("Мой микс", songs);
            MwtkPlaylists.playMessages(results, results.get(0));
        });
    }

    private void runImport(String link) {
        if (link == null || link.trim().isEmpty()) {
            return;
        }
        int generation = ++searchGeneration;
        MwtkUi.toast("Читаю плейлист…");
        MwtkImport.fetch(link.trim(), (name, songs, error) -> {
            if (generation != searchGeneration || getParentActivity() == null) {
                return;
            }
            if (error != null) {
                MwtkUi.toast(error);
                return;
            }
            MwtkUi.toast("Ищу " + songs.size() + " песен в музыке Telegram…");
            MwtkImport.findAll(currentAccount, songs, found -> {
                if (generation != searchGeneration || getParentActivity() == null) {
                    return;
                }
                if (found.isEmpty()) {
                    MwtkUi.toast("В музыке Telegram ничего из плейлиста не нашлось");
                    return;
                }
                String title = name == null || name.trim().isEmpty() ? "Перенесённый плейлист" : name.trim();
                showResults(title, found);
                MwtkUi.confirm(this, "Нашлось " + found.size() + " из " + songs.size(),
                    "Сохранить плейлист «" + title + "»? Песни лягут в «Избранное» и в новый плейлист.",
                    "Сохранить", () -> MwtkImport.saveAll(this, found, title));
            });
        });
    }

    private void runSearch(String query) {
        if (query.isEmpty()) {
            return;
        }
        if (searchRequest != 0) {
            ConnectionsManager.getInstance(currentAccount).cancelRequest(searchRequest, true);
            searchRequest = 0;
        }
        int generation = ++searchGeneration;
        MwtkUi.toast("Ищу…");
        final int account = currentAccount;
        // First the official music search of Telegram, then the chats you are in.
        MwtkMusicBot.search(account, query, botSongs -> {
            if (generation != searchGeneration) {
                return;
            }
            TLRPC.TL_messages_searchGlobal req = new TLRPC.TL_messages_searchGlobal();
            req.filter = new TLRPC.TL_inputMessagesFilterMusic();
            req.q = query;
            req.limit = 40;
            req.offset_rate = 0;
            req.offset_id = 0;
            req.offset_peer = new TLRPC.TL_inputPeerEmpty();
            searchRequest = ConnectionsManager.getInstance(account).sendRequest(req, (res, err) -> AndroidUtilities.runOnUIThread(() -> {
                searchRequest = 0;
                if (generation != searchGeneration) {
                    return;
                }
                ArrayList<MessageObject> found = new ArrayList<>(botSongs);
                if (res instanceof TLRPC.messages_Messages) {
                    TLRPC.messages_Messages r = (TLRPC.messages_Messages) res;
                    MessagesController.getInstance(account).putUsers(r.users, false);
                    MessagesController.getInstance(account).putChats(r.chats, false);
                    for (TLRPC.Message message : r.messages) {
                        MessageObject object = new MessageObject(account, message, false, true);
                        if (object.isMusic()) {
                            found.add(object);
                        }
                    }
                }
                if (found.isEmpty()) {
                    MwtkUi.toast(err != null && botSongs.isEmpty() ? "Не получилось выполнить поиск" : "Ничего не нашлось");
                    return;
                }
                showResults("Найдено", found);
            }));
        });
    }

    // ------------------------------------------------------------ player

    private View createPlayer(Context context) {
        FrameLayout box = new FrameLayout(context);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xeb1e080d);
        bg.setCornerRadius(AndroidUtilities.dp(18));
        bg.setStroke(AndroidUtilities.dp(1), 0x40ff2d46);
        box.setBackground(bg);

        playerCover = new BackupImageView(context);
        playerCover.setRoundRadius(AndroidUtilities.dp(10));
        box.addView(playerCover, LayoutHelper.createFrame(46, 46, Gravity.LEFT | Gravity.CENTER_VERTICAL, 10, 0, 0, 0));

        LinearLayout texts = new LinearLayout(context);
        texts.setOrientation(LinearLayout.VERTICAL);
        playerTitle = new TextView(context);
        playerTitle.setTextColor(0xffffffff);
        playerTitle.setTextSize(15);
        playerTitle.setTypeface(AndroidUtilities.bold());
        playerTitle.setSingleLine(true);
        playerTitle.setEllipsize(TextUtils.TruncateAt.END);
        texts.addView(playerTitle);
        playerArtist = new TextView(context);
        playerArtist.setTextColor(DIM);
        playerArtist.setTextSize(13);
        playerArtist.setSingleLine(true);
        playerArtist.setEllipsize(TextUtils.TruncateAt.END);
        texts.addView(playerArtist);
        box.addView(texts, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, Gravity.LEFT | Gravity.CENTER_VERTICAL, 66, 0, 150, 0));

        LinearLayout controls = new LinearLayout(context);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        controls.setGravity(Gravity.CENTER_VERTICAL);
        TextView lyricsButton = control(context, "≡");
        lyricsButton.setOnClickListener(v -> {
            lyricsView.setVisibility(View.VISIBLE);
            lyricsView.invalidate();
        });
        controls.addView(lyricsButton, LayoutHelper.createLinear(40, 40));
        TextView prev = control(context, "⏮");
        prev.setOnClickListener(v -> MediaController.getInstance().playPreviousMessage());
        controls.addView(prev, LayoutHelper.createLinear(36, 40));
        playerPlay = control(context, "⏸");
        GradientDrawable playBg = new GradientDrawable();
        playBg.setShape(GradientDrawable.OVAL);
        playBg.setColor(ACCENT);
        playerPlay.setBackground(playBg);
        playerPlay.setOnClickListener(v -> {
            MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
            if (playing == null) {
                return;
            }
            if (MediaController.getInstance().isMessagePaused()) {
                MediaController.getInstance().playMessage(playing);
            } else {
                MediaController.getInstance().pauseMessage(playing);
            }
            tick();
        });
        controls.addView(playerPlay, LayoutHelper.createLinear(40, 40, 4, 0, 4, 0));
        TextView next = control(context, "⏭");
        next.setOnClickListener(v -> MediaController.getInstance().playNextMessage());
        controls.addView(next, LayoutHelper.createLinear(36, 40));
        box.addView(controls, LayoutHelper.createFrame(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, Gravity.RIGHT | Gravity.CENTER_VERTICAL, 0, 0, 8, 0));

        playerBar = new View(context) {
            private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

            @Override
            protected void onDraw(Canvas canvas) {
                MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
                float progress = playing != null ? playing.audioProgress : 0;
                paint.setColor(0x1fffffff);
                canvas.drawRect(0, 0, getWidth(), getHeight(), paint);
                paint.setColor(ACCENT);
                canvas.drawRect(0, 0, getWidth() * Math.max(0, Math.min(1, progress)), getHeight(), paint);
            }
        };
        box.addView(playerBar, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, 3, Gravity.BOTTOM, 14, 0, 14, 4));
        box.setOnClickListener(v -> {
            lyricsView.setVisibility(View.VISIBLE);
            lyricsView.invalidate();
        });
        return box;
    }

    private TextView control(Context context, String text) {
        TextView view = new TextView(context);
        view.setText(text);
        view.setTextColor(0xffffffff);
        view.setTextSize(18);
        view.setGravity(Gravity.CENTER);
        return view;
    }

    private MessageObject shownPlaying;

    private void tick() {
        AndroidUtilities.cancelRunOnUIThread(tick);
        if (player == null) {
            return;
        }
        MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
        boolean music = playing != null && playing.isMusic();
        player.setVisibility(music ? View.VISIBLE : View.GONE);
        if (music) {
            if (playing != shownPlaying) {
                shownPlaying = playing;
                playerTitle.setText(playing.getMusicTitle());
                playerArtist.setText(playing.getMusicAuthor());
                setCover(playerCover, playing, playing.getMusicTitle());
                lyricsView.bind(playing);
            }
            playerPlay.setText(MediaController.getInstance().isMessagePaused() ? "▶" : "⏸");
            playerBar.invalidate();
            if (lyricsView.getVisibility() == View.VISIBLE) {
                lyricsView.invalidate();
            }
        } else if (lyricsView.getVisibility() == View.VISIBLE) {
            lyricsView.setVisibility(View.GONE);
        }
        AndroidUtilities.runOnUIThread(tick, lyricsView.getVisibility() == View.VISIBLE ? 33 : 250);
    }

    // ------------------------------------------------------------ rows

    @SuppressLint("ViewConstructor")
    private final class TrackRow extends FrameLayout {
        final BackupImageView cover;
        final TextView title;
        final TextView subtitle;
        final TextView time;
        final TextView action;
        final TextView extra;
        private String seed;

        TrackRow(Context context) {
            super(context);
            setBackground(Theme.getSelectorDrawable(false));
            cover = new BackupImageView(context);
            cover.setRoundRadius(AndroidUtilities.dp(10));
            addView(cover, LayoutHelper.createFrame(46, 46, Gravity.LEFT | Gravity.CENTER_VERTICAL, 14, 0, 0, 0));
            LinearLayout texts = new LinearLayout(context);
            texts.setOrientation(LinearLayout.VERTICAL);
            title = new TextView(context);
            title.setTextColor(0xffffffff);
            title.setTextSize(15);
            title.setTypeface(AndroidUtilities.bold());
            title.setSingleLine(true);
            title.setEllipsize(TextUtils.TruncateAt.END);
            texts.addView(title);
            subtitle = new TextView(context);
            subtitle.setTextColor(DIM);
            subtitle.setTextSize(13);
            subtitle.setSingleLine(true);
            subtitle.setEllipsize(TextUtils.TruncateAt.END);
            texts.addView(subtitle);
            addView(texts, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, Gravity.LEFT | Gravity.CENTER_VERTICAL, 72, 0, 96, 0));
            time = new TextView(context);
            time.setTextColor(DIM);
            time.setTextSize(13);
            addView(time, LayoutHelper.createFrame(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, Gravity.RIGHT | Gravity.CENTER_VERTICAL, 0, 0, 48, 0));
            action = new TextView(context);
            action.setTextColor(DIM);
            action.setTextSize(20);
            action.setGravity(Gravity.CENTER);
            addView(action, LayoutHelper.createFrame(40, 40, Gravity.RIGHT | Gravity.CENTER_VERTICAL, 0, 0, 6, 0));
            extra = new TextView(context);
            extra.setTextColor(DIM);
            extra.setTextSize(18);
            extra.setGravity(Gravity.CENTER);
            extra.setVisibility(GONE);
            addView(extra, LayoutHelper.createFrame(40, 40, Gravity.RIGHT | Gravity.CENTER_VERTICAL, 0, 0, 46, 0));
        }

        /** A second button left of «＋» («👎» in «Мой микс»). */
        void showExtra(String text) {
            extra.setText(text);
            extra.setVisibility(VISIBLE);
            time.setLayoutParams(LayoutHelper.createFrame(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, Gravity.RIGHT | Gravity.CENTER_VERTICAL, 0, 0, 90, 0));
            View texts = getChildAt(1);
            texts.setLayoutParams(LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, Gravity.LEFT | Gravity.CENTER_VERTICAL, 72, 0, 136, 0));
        }

        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            super.onMeasure(widthMeasureSpec, MeasureSpec.makeMeasureSpec(AndroidUtilities.dp(62), MeasureSpec.EXACTLY));
        }

        void bind(MessageObject message, String titleText, String performer, int seconds, String actionText) {
            seed = titleText;
            title.setText(TextUtils.isEmpty(titleText) ? "Без названия" : titleText);
            subtitle.setText(performer == null ? "" : performer);
            time.setText(seconds > 0 ? duration(seconds) : "");
            action.setText(actionText);
            setMessage(message);
        }

        MessageObject boundMessage;

        void setMessage(MessageObject message) {
            boundMessage = message;
            setCover(cover, message, seed);
            if (message != null && MwtkLyrics.hasCached(message)) {
                CharSequence text = title.getText();
                if (!text.toString().endsWith(" · ТЕКСТ")) {
                    title.setText(text + " · ТЕКСТ");
                }
            }
        }
    }

    /** Four covers of the first tracks, or a gradient with the name. */
    @SuppressLint("ViewConstructor")
    private final class Collage extends FrameLayout {
        private final BackupImageView[] cells = new BackupImageView[4];

        Collage(Context context) {
            super(context);
            for (int i = 0; i < 4; i++) {
                cells[i] = new BackupImageView(context);
                addView(cells[i]);
            }
            setClipToOutline(true);
            setOutlineProvider(new android.view.ViewOutlineProvider() {
                @Override
                public void getOutline(View view, android.graphics.Outline outline) {
                    outline.setRoundRect(0, 0, view.getWidth(), view.getHeight(), AndroidUtilities.dp(16));
                }
            });
        }

        @Override
        protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
            int w = right - left;
            int h = bottom - top;
            for (int i = 0; i < 4; i++) {
                int x = (i % 2) * w / 2;
                int y = (i / 2) * h / 2;
                cells[i].layout(x, y, x + w / 2, y + h / 2);
            }
        }

        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec);
            int w = getMeasuredWidth();
            int h = getMeasuredHeight();
            for (BackupImageView cell : cells) {
                cell.measure(MeasureSpec.makeMeasureSpec(w / 2, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(h / 2, MeasureSpec.EXACTLY));
            }
        }

        void bind(int index) {
            JSONArray tracks = MwtkPlaylists.tracks(index);
            for (int i = 0; i < 4; i++) {
                JSONObject t = tracks.length() > 0 ? tracks.optJSONObject(i % tracks.length()) : null;
                String seed = t != null ? t.optString("title") : MwtkPlaylists.name(index) + i;
                MessageObject message = t != null ? MwtkPlaylists.cached(t.optLong("peer"), t.optLong("msg")) : null;
                cells[i].setRoundRadius(0);
                setCover(cells[i], message, seed);
            }
            if (tracks.length() > 0) {
                MwtkPlaylists.load(index, messages -> {
                    for (int i = 0; i < 4 && !messages.isEmpty(); i++) {
                        MessageObject message = messages.get(i % messages.size());
                        if (message != null) {
                            setCover(cells[i], message, message.getMusicTitle());
                        }
                    }
                });
            }
        }
    }

    // ------------------------------------------------------------ lyrics

    /** Full screen lyrics like in VK: the current line glows, a tap on a line seeks. */
    @SuppressLint("ViewConstructor")
    private final class LyricsView extends View {
        private final Paint background = new Paint();
        private final android.text.TextPaint text = new android.text.TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final android.text.TextPaint small = new android.text.TextPaint(Paint.ANTI_ALIAS_FLAG);
        private MessageObject message;
        private float scrollY;
        private final ArrayList<float[]> hit = new ArrayList<>();

        LyricsView(Context context) {
            super(context);
            text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            text.setTextSize(AndroidUtilities.dp(24));
            small.setTextSize(AndroidUtilities.dp(14));
            small.setColor(DIM);
            setClickable(true);
        }

        void bind(MessageObject message) {
            this.message = message;
            scrollY = 0;
        }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            background.setShader(new LinearGradient(0, 0, 0, h, 0xff2a0710, BG, Shader.TileMode.CLAMP));
        }

        @Override
        protected void onDraw(Canvas canvas) {
            canvas.drawRect(0, 0, getWidth(), getHeight(), background);
            float pad = AndroidUtilities.dp(22);
            float top = AndroidUtilities.dp(24);
            text.setColor(0xffffffff);
            text.setTextSize(AndroidUtilities.dp(18));
            String title = message != null ? message.getMusicTitle() : "";
            String artist = message != null ? message.getMusicAuthor() : "";
            canvas.drawText(TextUtils.ellipsize(title, text, getWidth() - pad * 2 - AndroidUtilities.dp(40), TextUtils.TruncateAt.END).toString(), pad, top + AndroidUtilities.dp(18), text);
            canvas.drawText(TextUtils.ellipsize(artist, small, getWidth() - pad * 2, TextUtils.TruncateAt.END).toString(), pad, top + AndroidUtilities.dp(40), small);
            text.setTextSize(AndroidUtilities.dp(26));
            canvas.drawText("⌄", getWidth() - pad - AndroidUtilities.dp(16), top + AndroidUtilities.dp(24), text);
            small.setColor(0x66ffffff);
            canvas.drawText("Зажми песню в чате → «Найти текст», «Исправить», «Сдвиг»", pad, getHeight() - AndroidUtilities.dp(24), small);
            small.setColor(DIM);

            ArrayList<MwtkLyrics.Line> lines = MwtkLyrics.linesFor(message);
            hit.clear();
            float areaTop = top + AndroidUtilities.dp(70);
            if (lines.isEmpty()) {
                text.setTextSize(AndroidUtilities.dp(20));
                text.setColor(0xffffffff);
                canvas.drawText("Текста пока нет", pad, areaTop + AndroidUtilities.dp(40), text);
                canvas.drawText("Зажми песню в чате → «Найти текст».", pad, areaTop + AndroidUtilities.dp(70), small);
                return;
            }
            long now = MwtkLyrics.positionFor(message);
            int current = -1;
            for (int i = 0; i < lines.size(); i++) {
                if (lines.get(i).at <= now) {
                    current = i;
                }
            }
            float lineGap = AndroidUtilities.dp(14);
            text.setTextSize(AndroidUtilities.dp(24));
            float maxWidth = getWidth() - pad * 2;
            // Measure every line (wrapped) to find where the current one sits.
            ArrayList<android.text.StaticLayout> layouts = new ArrayList<>();
            float y = 0;
            float currentY = 0;
            for (int i = 0; i < lines.size(); i++) {
                android.text.StaticLayout layout = new android.text.StaticLayout(lines.get(i).text, text, (int) maxWidth,
                    android.text.Layout.Alignment.ALIGN_NORMAL, 1f, 0f, false);
                layouts.add(layout);
                if (i == current) {
                    currentY = y;
                }
                y += layout.getHeight() + lineGap;
            }
            float target = Math.max(0, currentY - (getHeight() - areaTop) * 0.3f);
            scrollY += (target - scrollY) * 0.12f;
            canvas.save();
            canvas.clipRect(0, areaTop, getWidth(), getHeight() - AndroidUtilities.dp(50));
            y = areaTop - scrollY;
            for (int i = 0; i < layouts.size(); i++) {
                android.text.StaticLayout layout = layouts.get(i);
                if (y + layout.getHeight() > areaTop - AndroidUtilities.dp(40) && y < getHeight()) {
                    if (i == current) {
                        text.setColor(0xffffffff);
                        text.setShadowLayer(AndroidUtilities.dp(10), 0, 0, 0xccff2d46);
                    } else {
                        text.setColor(i < current ? 0x80ffffff : 0x47ffffff);
                        text.clearShadowLayer();
                    }
                    canvas.save();
                    canvas.translate(pad, y);
                    layout.draw(canvas);
                    canvas.restore();
                    hit.add(new float[] { y, y + layout.getHeight(), lines.get(i).at });
                }
                y += layout.getHeight() + lineGap;
            }
            text.clearShadowLayer();
            canvas.restore();
        }

        @Override
        public boolean onTouchEvent(android.view.MotionEvent event) {
            if (event.getAction() != android.view.MotionEvent.ACTION_UP) {
                return true;
            }
            float x = event.getX();
            float y = event.getY();
            if (y < AndroidUtilities.dp(80) && x > getWidth() - AndroidUtilities.dp(80)) {
                setVisibility(GONE);
                return true;
            }
            for (float[] h : hit) {
                if (y >= h[0] && y <= h[1] && message != null) {
                    MediaController.getInstance().seekToProgressMs(message, (long) h[2]);
                    return true;
                }
            }
            return true;
        }
    }
}
