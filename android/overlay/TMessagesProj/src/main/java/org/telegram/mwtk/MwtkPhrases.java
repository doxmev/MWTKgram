package org.telegram.mwtk;

import android.text.Editable;
import android.text.TextWatcher;
import android.widget.EditText;

import org.json.JSONArray;
import org.telegram.ui.ActionBar.AlertDialog;
import org.telegram.ui.ActionBar.BaseFragment;

import java.util.ArrayList;

/** Quick phrases: a lone "/" in the message field offers saved phrases. */
public final class MwtkPhrases {

    private static final String FILE = "phrases.json";

    private MwtkPhrases() {
    }

    public static ArrayList<String> list() {
        ArrayList<String> result = new ArrayList<>();
        try {
            String text = MwtkStore.readText(FILE);
            JSONArray array = text.isEmpty() ? new JSONArray() : new JSONArray(text);
            for (int i = 0; i < array.length(); i++) {
                String value = array.optString(i, "").trim();
                if (!value.isEmpty()) {
                    result.add(value);
                }
            }
        } catch (Throwable ignore) {
        }
        return result;
    }

    public static void setList(ArrayList<String> list) {
        JSONArray array = new JSONArray();
        for (String item : list) {
            if (item != null && !item.trim().isEmpty()) {
                array.put(item.trim());
            }
        }
        MwtkStore.writeText(FILE, array.toString());
    }

    public static JSONArray export() {
        JSONArray array = new JSONArray();
        for (String item : list()) {
            array.put(item);
        }
        return array;
    }

    public static void importAll(JSONArray array) {
        ArrayList<String> list = new ArrayList<>();
        for (int i = 0; i < array.length(); i++) {
            list.add(array.optString(i, ""));
        }
        setList(list);
    }

    public static void attach(BaseFragment fragment, EditText field) {
        if (field == null || fragment == null) {
            return;
        }
        field.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                MwtkConfig.load();
                if (!MwtkConfig.effects || !MwtkConfig.flag(MwtkConfig.PHRASES, true)) {
                    return;
                }
                if (!"/".equals(s.toString())) {
                    return;
                }
                ArrayList<String> phrases = list();
                if (fragment.getParentActivity() == null) {
                    return;
                }
                String[] items = new String[phrases.size() + 1];
                for (int i = 0; i < phrases.size(); i++) {
                    String title = phrases.get(i).replace('\n', ' ');
                    items[i] = title.length() > 48 ? title.substring(0, 46) + "…" : title;
                }
                items[phrases.size()] = "✏ Мои фразы…";
                AlertDialog.Builder builder = new AlertDialog.Builder(fragment.getParentActivity());
                builder.setTitle("Быстрые фразы");
                builder.setItems(items, (dialog, which) -> {
                    if (which == phrases.size()) {
                        edit(fragment);
                    } else {
                        field.setText(phrases.get(which));
                        field.setSelection(field.length());
                    }
                });
                fragment.showDialog(builder.create());
            }
        });
    }

    public static void edit(BaseFragment fragment) {
        StringBuilder current = new StringBuilder();
        for (String item : list()) {
            if (current.length() > 0) {
                current.append('\n');
            }
            current.append(item);
        }
        MwtkUi.input(fragment, "Быстрые фразы",
            "Каждая фраза с новой строки. В чате набери «/» — и выбери нужную.",
            current.toString(), "скоро буду", true, false, null, text -> {
                ArrayList<String> list = new ArrayList<>();
                for (String line : text.split("\n")) {
                    list.add(line);
                }
                setList(list);
            });
    }
}
