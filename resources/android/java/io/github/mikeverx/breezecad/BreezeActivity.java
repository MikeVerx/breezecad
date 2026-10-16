// BreezeCAD on Android: the NativeActivity that runs libbreezecad.so, plus
// what only Java can do (docs/ADR/0004-mobile-touch-ui.md): the system's file
// pickers (SAF), "share", the keyboard and its height, the screen's cut-out,
// corners and gesture areas, immersive mode, the crash watchdog and why the
// last run ended. The C++ side is src/platform/Android.cpp.
//
// Calls from C++ come on the app's own thread; anything that touches views
// goes to the UI thread. Answers go back through the native* methods.
package io.github.mikeverx.breezecad;

import android.Manifest;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Insets;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.RoundedCorner;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

public class BreezeActivity extends NativeActivity {
    // NativeActivity opens libbreezecad.so itself, outside Java's class
    // loader: the native* methods below would not be found (UnsatisfiedLinkError
    // on the first insets). Loaded here first; the activity then gets the same library.
    static {
        System.loadLibrary("breezecad");
    }

    static final String PREFS = "breezecad";
    private static final int PICK_OPEN = 1, PICK_SAVE = 2, PICK_FOLDER = 3;

    private TextInputView textInput;
    private boolean immersive = true;

    // --- Life -------------------------------------------------------------------

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Window window = getWindow();
        // The app draws under the cut-out and the bars: it lays its panels out
        // around them itself (ADR-0004, "Панель быстрого доступа у камеры").
        WindowManager.LayoutParams lp = window.getAttributes();
        lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        window.setAttributes(lp);
        if (Build.VERSION.SDK_INT >= 30) window.setDecorFitsSystemWindows(false);

        textInput = new TextInputView(this);
        addContentView(textInput, new ViewGroup.LayoutParams(1, 1));

        window.getDecorView().setOnApplyWindowInsetsListener((v, insets) -> {
            pushScreen(insets);
            return v.onApplyWindowInsets(insets);
        });
        applyImmersive();
        Watchdog.start(this);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) applyImmersive();   // the bars come back after a dialog or the keyboard
    }

    @Override
    protected void onResume() {
        super.onResume();
        Watchdog.setForeground(this, true);
    }

    @Override
    protected void onPause() {
        Watchdog.setForeground(this, false);
        super.onPause();
    }

    // --- Screen: bars, gestures, cut-out, keyboard, corners -----------------------

    private void pushScreen(WindowInsets insets) {
        float d = getResources().getDisplayMetrics().density;
        float[] bars = new float[4], gestures = new float[4], cutout = new float[4], ime = new float[4];
        float cutFrom = 0, cutTo = 0, corner = 0;
        if (Build.VERSION.SDK_INT >= 30) {
            fill(bars, insets.getInsets(WindowInsets.Type.systemBars()), d);
            fill(gestures, insets.getInsets(WindowInsets.Type.systemGestures()), d);
            fill(ime, insets.getInsets(WindowInsets.Type.ime()), d);
        } else {
            bars[0] = insets.getSystemWindowInsetLeft() / d;
            bars[1] = insets.getSystemWindowInsetTop() / d;
            bars[2] = insets.getSystemWindowInsetRight() / d;
            bars[3] = insets.getSystemWindowInsetBottom() / d;
            if (Build.VERSION.SDK_INT >= 29) {
                Insets g = insets.getSystemGestureInsets();
                gestures[0] = g.left / d; gestures[1] = g.top / d; gestures[2] = g.right / d; gestures[3] = g.bottom / d;
            }
        }
        DisplayCutout dc = insets.getDisplayCutout();
        if (dc != null) {
            cutout[0] = dc.getSafeInsetLeft() / d;
            cutout[1] = dc.getSafeInsetTop() / d;
            cutout[2] = dc.getSafeInsetRight() / d;
            cutout[3] = dc.getSafeInsetBottom() / d;
            // Where along its side the camera is: the panel at the cut-out
            // leaves a gap there (landscape: the cut-out is on the left or right).
            for (Rect r : dc.getBoundingRects()) {
                boolean side = r.left == 0 || r.right >= getWindow().getDecorView().getWidth() - 1;
                cutFrom = (side ? r.top : r.left) / d;
                cutTo = (side ? r.bottom : r.right) / d;
            }
        }
        if (Build.VERSION.SDK_INT >= 31) {
            RoundedCorner rc = insets.getRoundedCorner(RoundedCorner.POSITION_TOP_LEFT);
            if (rc != null) corner = rc.getRadius() / d;
        }
        boolean hardKeyboard = getResources().getConfiguration().keyboard == android.content.res.Configuration.KEYBOARD_QWERTY;
        nativeScreen(bars, gestures, cutout, cutFrom, cutTo, ime, corner, hardKeyboard);
    }

    private static void fill(float[] out, Insets in, float d) {
        out[0] = in.left / d; out[1] = in.top / d; out[2] = in.right / d; out[3] = in.bottom / d;
    }

    public void setImmersive(boolean on) {
        runOnUiThread(() -> { immersive = on; applyImmersive(); });
    }

    @SuppressWarnings("deprecation")
    private void applyImmersive() {
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = decor.getWindowInsetsController();
            if (c == null) return;
            if (immersive) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            } else {
                c.show(WindowInsets.Type.systemBars());
            }
        } else {
            decor.setSystemUiVisibility(immersive
                    ? View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                      | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    : View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    /// Rectangles (dp: left, top, right, bottom, …) where the app's own
    /// controls sit at an edge: the system's back swipe stays out of them.
    public void setGestureExclusion(float[] rects) {
        runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT < 29) return;
            float d = getResources().getDisplayMetrics().density;
            List<Rect> list = new ArrayList<>();
            for (int i = 0; i + 3 < rects.length; i += 4)
                list.add(new Rect((int) (rects[i] * d), (int) (rects[i + 1] * d), (int) (rects[i + 2] * d), (int) (rects[i + 3] * d)));
            getWindow().getDecorView().setSystemGestureExclusionRects(list);
        });
    }

    // --- Keyboard ------------------------------------------------------------

    public void showKeyboard(boolean show) {
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (show) {
                textInput.requestFocus();
                imm.showSoftInput(textInput, InputMethodManager.SHOW_IMPLICIT);
            } else {
                imm.hideSoftInputFromWindow(textInput.getWindowToken(), 0);
            }
        });
    }

    /// The character a hardware key makes with the modifiers held (0: none).
    public static int unicodeChar(int deviceId, int keyCode, int metaState) {
        try {
            return KeyCharacterMap.load(deviceId).get(keyCode, metaState);
        } catch (Exception e) {
            return 0;
        }
    }

    /// The invisible view the system keyboard types into: text goes to C++.
    static final class TextInputView extends View {
        TextInputView(Context c) {
            super(c);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onCheckIsTextEditor() { return true; }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo info) {
            // No suggestions or extracted full-screen editor: the field is in the app.
            info.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                    | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
            info.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_DONE;
            return new BaseInputConnection(this, false) {
                private String composing = "";

                @Override
                public boolean commitText(CharSequence text, int newCursorPosition) {
                    composing = "";
                    nativeText(text.toString(), 0);
                    return true;
                }

                @Override
                public boolean setComposingText(CharSequence text, int newCursorPosition) {
                    composing = text.toString();
                    return true;
                }

                @Override
                public boolean finishComposingText() {
                    if (!composing.isEmpty()) nativeText(composing, 0);
                    composing = "";
                    return true;
                }

                @Override
                public boolean deleteSurroundingText(int before, int after) {
                    for (int i = 0; i < before; ++i) nativeText("", KeyEvent.KEYCODE_DEL);
                    return true;
                }

                @Override
                public boolean sendKeyEvent(KeyEvent e) {
                    if (e.getAction() != KeyEvent.ACTION_DOWN) return true;
                    int code = e.getKeyCode();
                    if (code == KeyEvent.KEYCODE_DEL || code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_FORWARD_DEL
                            || code == KeyEvent.KEYCODE_TAB) {
                        nativeText("", code);
                    } else {
                        int ch = e.getUnicodeChar();
                        if (ch != 0) nativeText(new String(Character.toChars(ch)), 0);
                    }
                    return true;
                }

                @Override
                public boolean performEditorAction(int action) {
                    nativeText("", KeyEvent.KEYCODE_ENTER);
                    return true;
                }
            };
        }
    }

    // --- Files: the projects folder and the system's pickers (SAF) -----------------

    public String projectsFolder() {
        return getSharedPreferences(PREFS, MODE_PRIVATE).getString("projects", "");
    }

    public void chooseProjectsFolder() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(i, PICK_FOLDER);
        });
    }

    public void pickToOpen(String[] mimeTypes) {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            if (mimeTypes != null && mimeTypes.length > 0) i.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
            startActivityForResult(i, PICK_OPEN);
        });
    }

    public void pickToSave(String name, String mime, String local) {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType(mime);
            i.putExtra(Intent.EXTRA_TITLE, name);
            startActivityForResult(i, PICK_SAVE);
        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        Uri uri = result == RESULT_OK && data != null ? data.getData() : null;
        int kind = request == PICK_OPEN ? 0 : request == PICK_SAVE ? 1 : 2;
        if (uri == null) {
            nativePicked(kind, "", "");
            return;
        }
        ContentResolver r = getContentResolver();
        try {
            int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            if (request == PICK_OPEN) flags = Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | ((data.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0 ? Intent.FLAG_GRANT_WRITE_URI_PERMISSION : 0);
            r.takePersistableUriPermission(uri, flags);
        } catch (Exception ignored) {
            // A provider without persistable grants: works for this run.
        }
        if (request == PICK_FOLDER)
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString("projects", uri.toString()).apply();
        nativePicked(kind, uri.toString(), request == PICK_FOLDER ? folderName(uri) : displayName(uri));
    }

    private String folderName(Uri tree) {
        String id = DocumentsContract.getTreeDocumentId(tree);
        int colon = id.lastIndexOf(':');
        return colon >= 0 ? id.substring(colon + 1) : id;
    }

    public String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        }
        String last = uri.getLastPathSegment();
        return last != null ? last : "document";
    }

    /// Documents in the projects folder: name, URI and modification time, flat.
    public String[] listProjects() {
        String tree = projectsFolder();
        if (tree.isEmpty()) return new String[0];
        Uri treeUri = Uri.parse(tree);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, DocumentsContract.getTreeDocumentId(treeUri));
        List<String> out = new ArrayList<>();
        String[] cols = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                         DocumentsContract.Document.COLUMN_LAST_MODIFIED};
        try (Cursor c = getContentResolver().query(children, cols, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String name = c.getString(1);
                if (name == null) continue;
                String lower = name.toLowerCase();
                if (!lower.endsWith(".bzc") && !lower.endsWith(".bza")) continue;
                out.add(name);
                out.add(DocumentsContract.buildDocumentUriUsingTree(treeUri, c.getString(0)).toString());
                out.add(Long.toString(c.getLong(2)));
            }
        } catch (Exception e) {
            return new String[0];
        }
        return out.toArray(new String[0]);
    }

    /// A new (or the existing) document named `name` in the projects folder.
    public String createInProjects(String name) {
        String tree = projectsFolder();
        if (tree.isEmpty()) return "";
        String[] list = listProjects();
        for (int i = 0; i + 2 < list.length; i += 3)
            if (list[i].equals(name)) return list[i + 1];
        Uri treeUri = Uri.parse(tree);
        try {
            Uri dir = DocumentsContract.buildDocumentUriUsingTree(treeUri, DocumentsContract.getTreeDocumentId(treeUri));
            Uri made = DocumentsContract.createDocument(getContentResolver(), dir, "application/octet-stream", name);
            return made != null ? made.toString() : "";
        } catch (Exception e) {
            return "";
        }
    }

    public boolean copyFromUri(String uri, String local) {
        try (InputStream in = getContentResolver().openInputStream(Uri.parse(uri));
             OutputStream out = new FileOutputStream(local)) {
            if (in == null) return false;
            copy(in, out);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public boolean copyToUri(String local, String uri) {
        try (InputStream in = new FileInputStream(local);
             OutputStream out = getContentResolver().openOutputStream(Uri.parse(uri), "wt")) {
            if (out == null) return false;
            copy(in, out);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    static void copy(InputStream in, OutputStream out) throws java.io.IOException {
        byte[] buffer = new byte[64 * 1024];
        for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
    }

    // --- Share ---------------------------------------------------------------

    public void share(String path, String mime, String subject) {
        runOnUiThread(() -> {
            try {
                startActivity(ShareProvider.chooser(this, new File(path), mime, subject));
            } catch (Exception ignored) {
            }
        });
    }

    public void setClipboard(String text) {
        runOnUiThread(() -> {
            android.content.ClipboardManager cm = (android.content.ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            cm.setPrimaryClip(ClipData.newPlainText("BreezeCAD", text));
        });
    }

    public String clipboard() {
        android.content.ClipboardManager cm = (android.content.ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clip = cm.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) return "";
        CharSequence text = clip.getItemAt(0).coerceToText(this);
        return text != null ? text.toString() : "";
    }

    public boolean openUrl(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /// The test run's commands (scripts/android/smoke-apk.sh starts the app
    /// with "--es bz example,sketch"); empty for a person.
    public String testCommands() {
        String s = getIntent() != null ? getIntent().getStringExtra("bz") : null;
        return s != null ? s : "";
    }

    // --- Crashes, version, permissions ---------------------------------------

    /// Why the last run of the main process ended (Android 11+), once: the
    /// same exit is not reported twice.
    public String lastExitReason() {
        if (Build.VERSION.SDK_INT < 30) return "";
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(getPackageName(), 0, 5);
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        long seen = prefs.getLong("exitSeen", 0);
        for (ApplicationExitInfo e : list) {
            if (!getPackageName().equals(e.getProcessName())) continue;   // not the ":crash" watchdog
            if (e.getTimestamp() <= seen) return "";
            prefs.edit().putLong("exitSeen", e.getTimestamp()).apply();
            switch (e.getReason()) {
                case ApplicationExitInfo.REASON_CRASH:        return "crash";
                case ApplicationExitInfo.REASON_CRASH_NATIVE: return "native crash";
                case ApplicationExitInfo.REASON_ANR:          return "anr";
                case ApplicationExitInfo.REASON_LOW_MEMORY:   return "low memory";
                case ApplicationExitInfo.REASON_USER_REQUESTED:
                case ApplicationExitInfo.REASON_USER_STOPPED: return "user";
                case ApplicationExitInfo.REASON_EXIT_SELF:    return "self";
                case ApplicationExitInfo.REASON_SIGNALED:     return "signaled";
                default:                                      return "other";
            }
        }
        return "";
    }

    public void askNotifications() {
        if (Build.VERSION.SDK_INT < 33) return;
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) return;
        runOnUiThread(() -> requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 7));
    }

    @SuppressWarnings("deprecation")
    public String packageVersion() {
        try {
            PackageInfo p = getPackageManager().getPackageInfo(getPackageName(), 0);
            long code = Build.VERSION.SDK_INT >= 28 ? p.getLongVersionCode() : p.versionCode;
            return p.versionName + "\t" + code;
        } catch (Exception e) {
            return "";
        }
    }

    // --- C++ (src/platform/Android.cpp) ----------------------------------------

    static native void nativeScreen(float[] bars, float[] gestures, float[] cutout, float cutoutFrom, float cutoutTo,
                                    float[] ime, float corner, boolean hardKeyboard);
    static native void nativeText(String text, int keyCode);
    static native void nativePicked(int kind, String uri, String name);
}
