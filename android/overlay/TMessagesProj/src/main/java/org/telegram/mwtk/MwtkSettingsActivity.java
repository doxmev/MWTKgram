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

    private void build(Context context) {
        header(context, "Главное");
        toggle(context, "Все эффекты MWTKgram", "Можно встряхнуть телефон, чтобы выключить", MwtkConfig.EFFECTS);
        cycle(context, "Тема", PRESETS, MwtkConfig.PRESET, true);
        cycle(context, "Стиль рисовки", STYLES, MwtkConfig.STYLE, false);
        shadow(context);

        header(context, "Фон");
        toggle(context, "Плывущие надписи", "doxmev и MWTK на фоне списка чатов", MwtkConfig.LABELS);
        toggle(context, "3D-фигуры", "Вращающиеся каркасные кубы", MwtkConfig.FIGURES);
        toggle(context, "Фон следует за наклоном", "Гироскоп двигает надписи и фигуры", MwtkConfig.TILT);
        toggle(context, "Беречь батарею", "Анимации на паузе в режиме энергосбережения и ниже 20%", MwtkConfig.BATTERY);
        shadow(context);

        header(context, "Список чатов");
        toggle(context, "Объёмные карточки чатов", "Полупрозрачные скруглённые карточки", MwtkConfig.CARDS);
        slider(context, "Скругление карточек", MwtkConfig.RADIUS, 0, 28, " dp");
        shadow(context);

        header(context, "Кто в сети");
        toggle(context, "Свечение аватарки", "Мягкий ореол вокруг", MwtkConfig.GLOW);
        toggle(context, "Полоска на карточке", "Светится у левого края", MwtkConfig.STRIPE);
        toggle(context, "Подсветка карточки", "Карточка светится изнутри", MwtkConfig.CARD_GLOW);
        toggle(context, "Точка", "Зелёная точка у аватарки", MwtkConfig.DOT);
        toggle(context, "Пульсирующая точка", "Медленно «дышит»", MwtkConfig.PULSE);
        toggle(context, "Аватарка-плитка", "Квадрат со скруглёнными углами", MwtkConfig.TILE);
        shadow(context);

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

    private void slider(Context context, String title, String key, int min, int max, String suffix) {
        LinearLayout box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setBackgroundColor(Theme.getColor(Theme.key_windowBackgroundWhite));
        box.setPadding(AndroidUtilities.dp(21), AndroidUtilities.dp(10), AndroidUtilities.dp(21), AndroidUtilities.dp(8));

        TextView label = new TextView(context);
        label.setTextSize(16);
        label.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        int value = MwtkConfig.getInt(key);
        label.setText(title + ": " + value + suffix);
        box.addView(label, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));

        SeekBar seek = new SeekBar(context);
        seek.setMax(max - min);
        seek.setProgress(value - min);
        if (Build.VERSION.SDK_INT >= 21) {
            int accent = MwtkConfig.palette()[2];
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
                MwtkConfig.setInt(key, bar.getProgress() + min);
                if (MwtkConfig.VIBRATION.equals(key)) {
                    MwtkMotion.vibrate(40);
                }
            }
        });
        box.addView(seek, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 40));
        list.addView(box, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT));
    }
}
