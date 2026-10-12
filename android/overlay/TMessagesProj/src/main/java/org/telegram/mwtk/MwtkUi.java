package org.telegram.mwtk;

import android.content.Context;
import android.text.InputType;
import android.util.TypedValue;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.Toast;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.ApplicationLoader;
import org.telegram.ui.ActionBar.AlertDialog;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.ActionBar.Theme;
import org.telegram.ui.Components.LayoutHelper;

/** Small dialogs used by the MWTK features. */
public final class MwtkUi {

    public interface TextCallback {
        void run(String text);
    }

    private MwtkUi() {
    }

    public static void toast(String text) {
        try {
            Toast.makeText(ApplicationLoader.applicationContext, text, Toast.LENGTH_SHORT).show();
        } catch (Throwable ignore) {
        }
    }

    public static EditText field(Context context, String value, String hint, boolean multiline, boolean password) {
        EditText field = new EditText(context);
        field.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 17);
        field.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        field.setHintTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteHintText));
        field.setHint(hint);
        if (password) {
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        } else if (multiline) {
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
            field.setMinLines(2);
            field.setMaxLines(8);
        } else {
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
            field.setSingleLine(true);
        }
        if (value != null) {
            field.setText(value);
            field.setSelection(field.length());
        }
        return field;
    }

    /** Text input dialog; "remove" button is shown when removeText != null. */
    public static void input(BaseFragment fragment, String title, String message, String value, String hint,
                             boolean multiline, boolean password, String removeText, TextCallback done) {
        Context context = fragment.getParentActivity();
        if (context == null) {
            return;
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle(title);
        if (message != null) {
            builder.setMessage(message);
        }
        EditText field = field(context, value, hint, multiline, password);
        FrameLayout box = new FrameLayout(context);
        box.addView(field, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 24, 6, 24, 0));
        builder.setView(box);
        builder.setPositiveButton("Готово", (dialog, which) -> done.run(field.getText().toString()));
        builder.setNegativeButton("Отмена", null);
        if (removeText != null) {
            builder.setNeutralButton(removeText, (dialog, which) -> done.run(""));
        }
        fragment.showDialog(builder.create());
        AndroidUtilities.runOnUIThread(() -> {
            field.requestFocus();
            AndroidUtilities.showKeyboard(field);
        }, 200);
    }

    public static void confirm(BaseFragment fragment, String title, String message, String ok, Runnable done) {
        Context context = fragment.getParentActivity();
        if (context == null) {
            return;
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(context);
        builder.setTitle(title);
        builder.setMessage(message);
        builder.setPositiveButton(ok, (dialog, which) -> done.run());
        builder.setNegativeButton("Отмена", null);
        fragment.showDialog(builder.create());
    }
}
