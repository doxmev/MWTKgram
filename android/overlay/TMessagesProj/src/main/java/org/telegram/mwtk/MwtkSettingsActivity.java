package org.telegram.mwtk;

import android.content.Context;
import android.content.res.ColorStateList;
import android.os.Build;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.R;
import org.telegram.ui.ActionBar.ActionBar;
import org.telegram.ui.ActionBar.AlertDialog;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.ActionBar.Theme;
import org.telegram.ui.Cells.HeaderCell;
import org.telegram.ui.Cells.ShadowSectionCell;
import org.telegram.ui.Cells.TextCheckCell;
import org.telegram.ui.Cells.TextInfoPrivacyCell;
import org.telegram.ui.Cells.TextSettingsCell;
import org.telegram.ui.Components.LayoutHelper;

/** Settings → MWTKgram. */
public class MwtkSettingsActivity extends BaseFragment {

    private static final String[] PRESETS = { "Кровь", "Синтвейв", "Космос", "Эконом" };
    private static final String[] STYLES = { "Неон", "Стекло", "Пиксели", "Комикс" };
    private static final String[] NOTIFY = { "Как в Telegram", "Голограмма", "Неоновая карточка", "3D-блок" };
    private static final String[] LAYOUTS = { "По словам", "Лесенка", "Строкой" };
    private static final String[] VANISH = { "Искры", "Растворение", "Улетает вверх" };
    private static final String[] TEXT_STYLES = { "Неон", "3D", "Пиксели" };
    private static final String[] APPEAR = { "Вразброс", "Из центра", "С краёв", "Снизу", "Из обложки" };
    private static final String[] DRIFT = { "Плавание", "Волна", "Спираль", "Дрожь в бит" };
    private static final String[] FLOAT_SOURCES = { "Последние", "Закреплённые", "Выбрать вручную" };
    private static final String[] COLORS = { "Как в теме", "Красный", "Розовый", "Фиолетовый", "Синий", "Бирюзовый", "Зелёный", "Жёлтый", "Белый" };
    private static final int[] COLOR_VALUES = { 0, 0xffe0141e, 0xffff2fa6, 0xff9b4dff, 0xff1f8bff, 0xff00d4c8, 0xff39ff88, 0xffffcc33, 0xffeeeeee };

    private LinearLayout list;

    @Override
    public View createView(Context context) {
        MwtkConfig.load();
        actionBar.setBackButtonImage(R.drawable.ic_ab_back);
        actionBar.setAllowOverlayTitle(true);
        actionBar.setTitle("MWTKgram");
        actionBar.setActionBarMenuOnItemClick(new ActionBar.ActionBarMenuOnItemClick() {
            @Override
            public void onItemClick(int id) {
                if (id == -1) {
                    finishFragment();
                }
            }
        });

        ScrollView scroll = new ScrollView(context);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Theme.getColor(Theme.key_windowBackgroundGray));
        list = new LinearLayout(context);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list, new ScrollView.LayoutParams(ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT));
        fragmentView = scroll;
        build(context);
        return fragmentView;
    }

    private void rebuild() {
        if (list == null || getParentActivity() == null) {
            return;
        }
        list.removeAllViews();
        build(getParentActivity());
    }

    private static final String[] CHAT_FX_MODES = { "Выкл.", "Поверх обоев" };

    private static final String[] TABS = { "Вид", "Музыка", "Чаты", "Ещё" };
    private static int tab;

    /** Tabs on top: «Вид», «Музыка», «Чаты», «Ещё»; only one page is shown. */
    private void tabs(Context context) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(AndroidUtilities.dp(10), AndroidUtilities.dp(10), AndroidUtilities.dp(10), AndroidUtilities.dp(10));
        row.setBackgroundColor(Theme.getColor(Theme.key_windowBackgroundWhite));
        int accent = MwtkConfig.accent();
        for (int i = 0; i < TABS.length; i++) {
            final int index = i;
            android.widget.TextView button = new android.widget.TextView(context);
            button.setText(TABS[i]);
            button.setGravity(android.view.Gravity.CENTER);
            button.setTextSize(15);
            button.setTypeface(AndroidUtilities.bold());
            android.graphics.drawable.GradientDrawable bg = new android.graphics.drawable.GradientDrawable();
            bg.setCornerRadius(AndroidUtilities.dp(12));
            if (i == tab) {
                bg.setColor(accent);
                button.setTextColor(0xffffffff);
            } else {
                bg.setColor(Theme.getColor(Theme.key_windowBackgroundGray));
                button.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
            }
            button.setBackground(bg);
            button.setOnClickListener(v -> {
                if (tab != index) {
                    tab = index;
                    MwtkMotion.vibrate(15);
                    rebuild();
                }
            });
            row.addView(button, LayoutHelper.createLinear(0, 40, 1f, i == 0 ? 0 : 6, 0, 0, 0));
        }
        list.addView(row, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void build(Context context) {
        header(context, "Главное");
        toggle(context, "Все эффекты MWTKgram", "Можно встряхнуть телефон, чтобы выключить", MwtkConfig.EFFECTS);
        tabs(context);
        shadow(context);
        switch (tab) {
            case 1: buildMusic(context); break;
            case 2: buildChats(context); break;
            case 3: buildOther(context); break;
            default: buildLook(context); break;
        }
    }

    private void buildLook(Context context) {
        header(context, "Оформление");
        cycle(context, "Тема", PRESETS, MwtkConfig.PRESET, true);
        cycle(context, "Стиль рисовки", STYLES, MwtkConfig.STYLE, false);
        flagCycle(context, "Свой цвет", COLORS, MwtkConfig.CUSTOM_COLOR, 0, COLOR_VALUES);
        flagCycle(context, "Иконка приложения", MwtkIcons.NAMES, MwtkConfig.APP_ICON, 0, null);
        flagCycle(context, "Уведомления в приложении", NOTIFY, MwtkConfig.NOTIFY_STYLE, 1, null);
        flag(context, "Искры при отправке", "Вспышка у кнопки отправки", MwtkConfig.SEND_SPARKS, true);
        flag(context, "Учитывать «Убрать анимации»", "Если в системе выключены анимации, эффекты замирают", MwtkConfig.RESPECT_ANIMATIONS, true);
        shadow(context);

        header(context, "Фон");
        flagCycle(context, "3D-сцена", MwtkBackgroundDrawable.SCENES, MwtkConfig.SCENE, 0, null);
        toggle(context, "Плывущие надписи", "doxmev и MWTK на фоне", MwtkConfig.LABELS);
        toggle(context, "3D-фигуры", "Вращающиеся каркасные кубы", MwtkConfig.FIGURES);
        flag(context, "Времена года", "Снег зимой, листья осенью, лепестки весной", MwtkConfig.SEASONS, true);
        flagCycle(context, "Эффекты в чате", CHAT_FX_MODES, MwtkConfig.CHAT_FX, 1, null);
        flag(context, "Режим планшета", "Справа 3D-сцена, текст песни на всю правую часть, чаты летают по всему экрану", MwtkConfig.TABLET, true);
        flag(context, "Секреты в 3 часа ночи", "Красная луна и глаза в лесу", MwtkConfig.NIGHT, true);
        toggle(context, "Фон следует за наклоном", "Гироскоп двигает надписи и фигуры", MwtkConfig.TILT);
        toggle(context, "Беречь батарею", "Пауза в энергосбережении, ниже 20% и при перегреве", MwtkConfig.BATTERY);
        shadow(context);

        header(context, "Прозрачность");
        numSlider(context, "Список чатов", MwtkConfig.LIST_ALPHA, 100, 10, 100, "%");
        numSlider(context, "Всё приложение", MwtkConfig.APP_ALPHA, 100, 15, 100, "%");
        shadow(context);

        header(context, "Карточки и «в сети»");
        toggle(context, "Объёмные карточки чатов", "Полупрозрачные скруглённые карточки", MwtkConfig.CARDS);
        slider(context, "Скругление карточек", MwtkConfig.RADIUS, 0, 28, " dp");
        toggle(context, "Свечение аватарки", "Мягкий ореол вокруг", MwtkConfig.GLOW);
        toggle(context, "Полоска на карточке", "Светится у левого края", MwtkConfig.STRIPE);
        toggle(context, "Подсветка карточки", "Карточка светится изнутри", MwtkConfig.CARD_GLOW);
        toggle(context, "Точка", "Зелёная точка у аватарки", MwtkConfig.DOT);
        toggle(context, "Пульсирующая точка", "Медленно «дышит»", MwtkConfig.PULSE);
        toggle(context, "Аватарка-плитка", "Квадрат со скруглёнными углами", MwtkConfig.TILE);
        flag(context, "Пролёт аватарки", "Новое сообщение пролетает над списком", MwtkConfig.FLYBY, true);
        flag(context, "Голографический профиль", "Весь профиль — голограмма цвета темы", MwtkConfig.HOLO, true);
        shadow(context);
    }

    private void buildMusic(Context context) {
        header(context, "Музыка");
        action(context, "Мои плейлисты", () -> MwtkPlaylists.showListBox(this));
        action(context, "Музыка, «Мой микс» и караоке", () -> presentFragment(new MwtkMusicActivity()));
        flag(context, "Фон в такт музыке", "Нужен доступ к микрофону (как для голосовых)", MwtkConfig.MUSIC_PULSE, true);
        flag(context, "Приглашения «Слушать вместе»", "Друг в MWTKgram зовёт слушать — тебя спросят", MwtkConfig.TOGETHER_ALLOW, true);
        flag(context, "Звуки интерфейса", "Мягкий звук при отправке", MwtkConfig.SOUNDS, true);
        shadow(context);

        header(context, "Текст песни");
        flag(context, "Летающий текст песни", "Слова вылетают и загораются в такт", MwtkConfig.LYRICS, true);
        flag(context, "Искать текст в интернете", "База LRCLIB", MwtkConfig.LYRICS_ONLINE, true);
        flag(context, "Спрятать чаты, пока играет текст", "Текст на весь экран, нажми — и чаты вернутся", MwtkConfig.HIDE_CHATS, false);
        flagCycle(context, "Расположение слов", LAYOUTS, MwtkConfig.LYRICS_LAYOUT, 0, null);
        flagCycle(context, "Как пропадают слова", VANISH, MwtkConfig.LYRICS_VANISH, 0, null);
        flagCycle(context, "Стиль текста", TEXT_STYLES, MwtkConfig.LYRICS_STYLE, 0, null);
        flagCycle(context, "Появление", APPEAR, MwtkConfig.LYRICS_APPEAR, 0, null);
        flagCycle(context, "Движение", DRIFT, MwtkConfig.LYRICS_DRIFT, 0, null);
        flag(context, "Текст дёргается в бит", "Слова подпрыгивают и дрожат под музыку", MwtkConfig.LYRICS_BEAT, true);
        numSlider(context, "Движение слов", MwtkConfig.LYRICS_MOTION, 60, 0, 100, "%");
        numSlider(context, "Размер текста", MwtkConfig.LYRICS_SIZE, 100, 60, 140, "%");
        TextInfoPrivacyCell musicInfo = new TextInfoPrivacyCell(context);
        musicInfo.setText("Долгое нажатие на трек в чате: «В плейлист MWTK», «Спрятать чаты», «Найти текст», «Синхронизировать текст» и «Сдвиг текста». Своя обложка — «⋯» у трека в плейлисте. Слушать вместе — меню чата (⋮).");
        list.addView(musicInfo, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void buildChats(Context context) {
        header(context, "Летающие чаты");
        numSlider(context, "Сколько чатов летает", MwtkConfig.FLOAT_COUNT, 16, 5, 40, "");
        flagCycle(context, "Какие чаты летают", FLOAT_SOURCES, MwtkConfig.FLOAT_SOURCE, 0, null);
        action(context, "Выбрать летающие чаты", () -> MwtkFloating.showPickDialog(this));
        TextInfoPrivacyCell floatingInfo = new TextInfoPrivacyCell(context);
        floatingInfo.setText("Летающие чаты: долгое нажатие на заголовок «MWTKgram» над списком. Бросай кружки пальцем, нажми на кружок — откроется чат, нажми мимо — чаты вернутся.");
        list.addView(floatingInfo, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));

        header(context, "Чаты и приватность");
        action(context, MwtkHidden.unlocked() ? "Закрыть скрытые чаты" : "Скрытые чаты", () -> {
            MwtkHidden.open(this);
            AndroidUtilities.runOnUIThread(this::rebuild, 400);
        });
        action(context, "Пароль скрытых чатов", () -> MwtkHidden.changePassword(this));
        flag(context, "Вход по отпечатку", "Для скрытых чатов", "hidden_fingerprint", true);
        flag(context, "Удалённые остаются в чате", "С красной пометкой «удалено»", MwtkConfig.KEEP_DELETED, true);
        flag(context, "Быстрые фразы через «/»", "Набери «/» в поле сообщения", MwtkConfig.PHRASES, true);
        action(context, "Мои быстрые фразы", () -> MwtkPhrases.edit(this));
        TextInfoPrivacyCell chatInfo = new TextInfoPrivacyCell(context);
        chatInfo.setText("В меню чата (⋮): «Скрыть чат», «Своё название», «Заметка», «Удалённые сообщения» и «Слушать вместе». У сообщения: «Напомнить». Подарок в архив — зажми его в своём профиле.");
        list.addView(chatInfo, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void buildOther(Context context) {
        header(context, "Синхронизация и темы");
        action(context, "Поделиться темой MWTK", () -> MwtkShare.exportTheme(getParentActivity()));
        action(context, "Отправить настройки на компьютер", () -> MwtkShare.exportSync(getParentActivity()));
        TextInfoPrivacyCell syncInfo = new TextInfoPrivacyCell(context);
        syncInfo.setText("Файл с компьютера (.mwtk) или тему (.mwtktheme) достаточно открыть в чате — MWTKgram предложит загрузить.");
        list.addView(syncInfo, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));

        header(context, "Встряхивание и вибрация");
        toggle(context, "Встряхнуть — выключить эффекты", "Ещё раз встряхнуть — включить", MwtkConfig.SHAKE);
        toggle(context, "Вибрация", "Отклик при встряхивании и в настройках", MwtkConfig.VIBRATE);
        slider(context, "Сила вибрации", MwtkConfig.VIBRATION, 0, 100, "%");

        TextInfoPrivacyCell info = new TextInfoPrivacyCell(context);
        info.setText("MWTKgram основан на открытом коде Telegram для Android. Все эффекты MWTK можно выключить, и тогда клиент выглядит как обычный Telegram.");
        list.addView(info, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void header(Context context, String title) {
        HeaderCell cell = new HeaderCell(context);
        cell.setText(title);
        cell.setBackgroundColor(Theme.getColor(Theme.key_windowBackgroundWhite));
        list.addView(cell, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void shadow(Context context) {
        list.addView(new ShadowSectionCell(context), LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void action(Context context, String title, Runnable callback) {
        TextSettingsCell cell = new TextSettingsCell(context);
        cell.setText(title, true);
        cell.setBackground(Theme.getSelectorDrawable(true));
        cell.setOnClickListener(v -> callback.run());
        list.addView(cell, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void toggle(Context context, String title, String hint, String key) {
        TextCheckCell cell = new TextCheckCell(context);
        cell.setTextAndValueAndCheck(title, hint, MwtkConfig.getBool(key), true, true);
        cell.setBackground(Theme.getSelectorDrawable(true));
        cell.setOnClickListener(v -> {
            boolean value = !MwtkConfig.getBool(key);
            MwtkConfig.setBool(key, value);
            cell.setChecked(value);
            MwtkMotion.vibrate(15);
            if (MwtkConfig.SHAKE.equals(key) || MwtkConfig.TILT.equals(key)) {
                MwtkMotion.get().pause();
                MwtkMotion.get().resume();
            }
            if (MwtkConfig.EFFECTS.equals(key)) {
                MwtkTheme.refresh();
            }
        });
        list.addView(cell, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void flag(Context context, String title, String hint, String key, boolean def) {
        TextCheckCell cell = new TextCheckCell(context);
        cell.setTextAndValueAndCheck(title, hint, MwtkConfig.flag(key, def), true, true);
        cell.setBackground(Theme.getSelectorDrawable(true));
        cell.setOnClickListener(v -> {
            boolean value = !MwtkConfig.flag(key, def);
            MwtkConfig.setFlag(key, value);
            cell.setChecked(value);
            MwtkMotion.vibrate(15);
        });
        list.addView(cell, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private void cycle(Context context, String title, String[] values, String key, boolean preset) {
        TextSettingsCell cell = new TextSettingsCell(context);
        int index = Math.max(0, Math.min(values.length - 1, MwtkConfig.getInt(key)));
        cell.setTextAndValue(title, values[index], true);
        cell.setBackground(Theme.getSelectorDrawable(true));
        cell.setOnClickListener(v -> {
            int next = (MwtkConfig.getInt(key) + 1) % values.length;
            if (preset) {
                MwtkConfig.applyPreset(next);
                MwtkMotion.vibrate(20);
                rebuild();
            } else {
                MwtkConfig.setInt(key, next);
                MwtkMotion.vibrate(15);
                cell.setTextAndValue(title, values[next], true, true);
            }
        });
        list.addView(cell, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    /** A choice from a list; values may map indexes to stored numbers. */
    private void flagCycle(Context context, String title, String[] names, String key, int def, int[] values) {
        TextSettingsCell cell = new TextSettingsCell(context);
        int stored = MwtkConfig.num(key, values != null ? values[def] : def);
        int index = 0;
        if (values != null) {
            for (int i = 0; i < values.length; i++) {
                if (values[i] == stored) {
                    index = i;
                }
            }
        } else {
            index = Math.max(0, Math.min(names.length - 1, stored));
        }
        cell.setTextAndValue(title, names[index], true);
        cell.setBackground(Theme.getSelectorDrawable(true));
        cell.setOnClickListener(v -> {
            AlertDialog.Builder builder = new AlertDialog.Builder(context);
            builder.setTitle(title);
            builder.setItems(names, (dialog, which) -> {
                MwtkConfig.setNum(key, values != null ? values[which] : which);
                if (MwtkConfig.APP_ICON.equals(key)) {
                    MwtkIcons.apply(which);
                }
                MwtkMotion.vibrate(15);
                cell.setTextAndValue(title, names[which], true, true);
            });
            showDialog(builder.create());
        });
        list.addView(cell, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }

    private interface IntGetter {
        int get();
    }

    private interface IntSetter {
        void set(int value);
    }

    private void slider(Context context, String title, String key, int min, int max, String suffix) {
        addSlider(context, title, min, max, suffix, () -> MwtkConfig.getInt(key), value -> {
            MwtkConfig.setInt(key, value);
            if (MwtkConfig.VIBRATION.equals(key)) {
                MwtkMotion.vibrate(40);
            }
        });
    }

    private void numSlider(Context context, String title, String key, int def, int min, int max, String suffix) {
        addSlider(context, title, min, max, suffix, () -> MwtkConfig.num(key, def), value -> {
            MwtkConfig.setNum(key, value);
            if (MwtkConfig.APP_ALPHA.equals(key)) {
                MwtkTheme.refresh();
            }
        });
    }

    private void addSlider(Context context, String title, int min, int max, String suffix, IntGetter getter, IntSetter setter) {
        LinearLayout box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setBackgroundColor(Theme.getColor(Theme.key_windowBackgroundWhite));
        box.setPadding(AndroidUtilities.dp(21), AndroidUtilities.dp(10), AndroidUtilities.dp(21), AndroidUtilities.dp(8));

        TextView label = new TextView(context);
        label.setTextSize(16);
        label.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        int value = getter.get();
        label.setText(title + ": " + value + suffix);
        box.addView(label, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));

        SeekBar seek = new SeekBar(context);
        seek.setMax(max - min);
        seek.setProgress(value - min);
        if (Build.VERSION.SDK_INT >= 21) {
            int accent = MwtkConfig.accent();
            seek.setProgressTintList(ColorStateList.valueOf(accent));
            seek.setThumbTintList(ColorStateList.valueOf(accent));
        }
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                label.setText(title + ": " + (progress + min) + suffix);
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar bar) {
                setter.set(bar.getProgress() + min);
            }
        });
        box.addView(seek, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 40));
        list.addView(box, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }
}
