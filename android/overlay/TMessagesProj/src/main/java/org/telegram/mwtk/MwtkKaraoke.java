package org.telegram.mwtk;

import android.annotation.SuppressLint;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.text.TextPaint;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.MediaController;
import org.telegram.messenger.MessageObject;
import org.telegram.ui.ActionBar.BaseFragment;

import java.util.ArrayList;

/**
 * Караоке on the whole screen: the line to sing in the middle filling word
 * by word, the next lines under it, a countdown before a line starts.
 */
public final class MwtkKaraoke {

    private MwtkKaraoke() {
    }

    public static void show(BaseFragment fragment) {
        Context context = fragment != null ? fragment.getParentActivity() : null;
        if (context == null) {
            return;
        }
        Dialog dialog = new Dialog(context, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        KaraokeView view = new KaraokeView(context, dialog);
        dialog.setContentView(view, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        Window window = dialog.getWindow();
        if (window != null) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
        dialog.show();
    }

    @SuppressLint("ViewConstructor")
    private static final class KaraokeView extends View {

        private final Dialog dialog;
        private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final long started = SystemClock.elapsedRealtime();

        KaraokeView(Context context, Dialog dialog) {
            super(context);
            this.dialog = dialog;
            text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        }

        @SuppressLint("ClickableViewAccessibility")
        @Override
        public boolean onTouchEvent(MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_UP) {
                dialog.dismiss();
            }
            return true;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            int w = getWidth();
            int h = getHeight();
            int accent = MwtkConfig.accent();
            float t = (SystemClock.elapsedRealtime() - started) / 1000f;
            int deep = Color.rgb(Color.red(accent) / 5, Color.green(accent) / 5, Color.blue(accent) / 5);
            paint.setShader(new RadialGradient(w * (0.5f + 0.15f * (float) Math.sin(t * 0.2f)), h * 0.45f,
                Math.max(w, h) * 0.8f, deep, 0xff060204, Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, w, h, paint);
            paint.setShader(null);

            text.setTextAlign(Paint.Align.CENTER);
            text.setTextSize(AndroidUtilities.dp(13));
            text.setColor(0x80ffffff);
            MessageObject playing = MediaController.getInstance().getPlayingMessageObject();
            if (playing != null && playing.isMusic()) {
                String author = playing.getMusicAuthor();
                canvas.drawText((author == null || author.isEmpty() ? "" : author + " — ") + playing.getMusicTitle(),
                    w / 2f, AndroidUtilities.statusBarHeight + AndroidUtilities.dp(28), text);
            }
            canvas.drawText("Караоке · нажми, чтобы выйти", w / 2f, h - AndroidUtilities.dp(24), text);

            ArrayList<MwtkLyrics.Line> lines = MwtkLyrics.currentLines();
            if (lines.isEmpty()) {
                text.setTextSize(AndroidUtilities.dp(20));
                text.setColor(0xffffffff);
                canvas.drawText("У этой песни нет текста", w / 2f, h / 2f, text);
                text.setTextSize(AndroidUtilities.dp(14));
                text.setColor(0x99ffffff);
                canvas.drawText("Зажми трек в чате → «Найти текст»", w / 2f, h / 2f + AndroidUtilities.dp(30), text);
                postInvalidateDelayed(500);
                return;
            }
            long now = MwtkLyrics.currentPosition();
            int current = -1;
            for (int i = 0; i < lines.size(); i++) {
                if (lines.get(i).at <= now) {
                    current = i;
                }
            }
            float big = Math.max(AndroidUtilities.dp(26), Math.min(w / 12f, AndroidUtilities.dp(48)));
            float centerY = h * 0.42f;

            // Countdown before the first line or after a long pause.
            int next = current + 1;
            if (next < lines.size()) {
                long wait = lines.get(next).at - now;
                boolean pause = current < 0 || now > lines.get(current).end + 1500;
                if (pause && wait > 0 && wait < 3200) {
                    int dots = (int) (wait / 1000) + 1;
                    for (int i = 0; i < 3; i++) {
                        paint.setColor(accent);
                        paint.setAlpha(i < dots ? 255 : 50);
                        canvas.drawCircle(w / 2f + (i - 1) * AndroidUtilities.dp(26), centerY - big * 1.7f, AndroidUtilities.dp(6), paint);
                    }
                    paint.setAlpha(255);
                }
            }
            // The previous line, small, above.
            if (current > 0) {
                text.setTextSize(big * 0.5f);
                text.setColor(0x46ffffff);
                drawWrapped(canvas, lines.get(current - 1).text, w / 2f, centerY - big * 2.2f, w * 0.88f);
            }
            float bottom = centerY;
            if (current >= 0) {
                bottom = drawSung(canvas, lines.get(current), now, big, centerY, w, accent);
            }
            // The next lines, under it.
            float y = bottom + big * 0.6f;
            text.setTextAlign(Paint.Align.CENTER);
            for (int i = current + 1; i <= current + 2 && i < lines.size(); i++) {
                text.setTextSize(big * 0.58f);
                text.setColor(i == current + 1 ? 0x8cffffff : 0x46ffffff);
                y = drawWrapped(canvas, lines.get(i).text, w / 2f, y, w * 0.88f) + big * 0.3f;
            }
            postInvalidateOnAnimation();
        }

        /** Centered text wrapped by words; returns the bottom. */
        private float drawWrapped(Canvas canvas, String value, float cx, float top, float maxWidth) {
            String[] words = value.split(" ");
            StringBuilder row = new StringBuilder();
            float lineHeight = text.getTextSize() * 1.25f;
            float y = top + text.getTextSize();
            for (String word : words) {
                String candidate = row.length() == 0 ? word : row + " " + word;
                if (row.length() > 0 && text.measureText(candidate) > maxWidth) {
                    canvas.drawText(row.toString(), cx, y, text);
                    y += lineHeight;
                    row = new StringBuilder(word);
                } else {
                    row = new StringBuilder(candidate);
                }
            }
            if (row.length() > 0) {
                canvas.drawText(row.toString(), cx, y, text);
            }
            return y + text.getTextSize() * 0.3f;
        }

        private float drawSung(Canvas canvas, MwtkLyrics.Line line, long now, float size, float centerY, int w, int accent) {
            text.setTextSize(size);
            text.setTextAlign(Paint.Align.LEFT);
            String[] words = line.text.trim().split("\\s+");
            long[] starts = new long[words.length];
            long[] ends = new long[words.length];
            if (line.words != null && line.words.length == words.length) {
                for (int i = 0; i < words.length; i++) {
                    starts[i] = line.words[i];
                    ends[i] = i + 1 < words.length ? line.words[i + 1] : line.end;
                }
            } else {
                int total = 0;
                for (String word : words) {
                    total += word.length() + 1;
                }
                double span = Math.max(line.end - line.at, 400) * 0.85;
                double at = line.at;
                for (int i = 0; i < words.length; i++) {
                    starts[i] = (long) at;
                    at += span * (words[i].length() + 1) / Math.max(total, 1);
                    ends[i] = (long) at;
                }
            }
            float space = text.measureText(" ");
            float maxWidth = w * 0.88f;
            ArrayList<ArrayList<Integer>> rows = new ArrayList<>();
            ArrayList<Integer> row = new ArrayList<>();
            float rowWidth = 0;
            for (int i = 0; i < words.length; i++) {
                float ww = text.measureText(words[i]);
                if (!row.isEmpty() && rowWidth + space + ww > maxWidth) {
                    rows.add(row);
                    row = new ArrayList<>();
                    rowWidth = 0;
                }
                rowWidth += (row.isEmpty() ? 0 : space) + ww;
                row.add(i);
            }
            rows.add(row);
            float rowHeight = size * 1.2f;
            float y = centerY - (rows.size() - 1) * rowHeight / 2f;
            int lit = Color.rgb(Math.min(255, Color.red(accent) + 70), Math.min(255, Color.green(accent) + 70), Math.min(255, Color.blue(accent) + 70));
            for (ArrayList<Integer> r : rows) {
                float full = 0;
                for (int i : r) {
                    full += text.measureText(words[i]);
                }
                full += space * (r.size() - 1);
                float x = (w - full) / 2f;
                for (int i : r) {
                    float ww = text.measureText(words[i]);
                    float baseline = y + size * 0.35f;
                    text.setColor(0x5affffff);
                    canvas.drawText(words[i], x, baseline, text);
                    float k = Math.max(0f, Math.min(1f, (now - starts[i]) / (float) Math.max(ends[i] - starts[i], 60)));
                    if (k > 0) {
                        canvas.save();
                        canvas.clipRect(x, y - rowHeight, x + ww * k, y + rowHeight);
                        text.setColor(lit);
                        canvas.drawText(words[i], x, baseline, text);
                        canvas.restore();
                    }
                    x += ww + space;
                }
                y += rowHeight;
            }
            text.setTextAlign(Paint.Align.CENTER);
            return y;
        }
    }
}
