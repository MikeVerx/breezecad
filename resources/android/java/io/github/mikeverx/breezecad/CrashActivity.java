// The error screen after a crash or a hang (ADR-0003 on the phone; ADR-0004,
// "Сбои и зависания на Android"): what happened, a code to copy, the report
// to share, and a way back into the program. Runs in the watchdog's process
// (":crash"): the app's own process is gone or stuck.
//
// The code is the desktop's (core/CrashCode.cpp), from the small library
// libbzcrash.so — not libbreezecad.so with all of OCCT.
package io.github.mikeverx.breezecad;

import android.app.Activity;
import android.app.NotificationManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

public class CrashActivity extends Activity {
    static final String EXTRA_HANG = "hang", EXTRA_PID = "pid", ACTION_WAIT = "io.github.mikeverx.breezecad.WAIT";

    static {
        try {
            System.loadLibrary("bzcrash");
        } catch (Throwable ignored) {
            // Without it the screen still shows the report, with a simpler code.
        }
    }

    static native String nativeErrorCode(String report);

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (ACTION_WAIT.equals(getIntent().getAction())) {   // "Подождать": only the notice goes
            getSystemService(NotificationManager.class).cancel(Watchdog.HANG_NOTICE);
            finish();
            return;
        }
        boolean hang = getIntent().getBooleanExtra(EXTRA_HANG, false);
        int pid = getIntent().getIntExtra(EXTRA_PID, 0);
        File report = newest(hang ? "hang-" : "crash-");
        if (hang) {
            if (report == null) report = writeHangReport();
            if (pid > 0) android.os.Process.killProcess(pid);   // "Закрыть и отправить отчёт"
            getSystemService(NotificationManager.class).cancel(Watchdog.HANG_NOTICE);
        }
        String text = report != null ? read(report) : "";
        String code;
        try {
            code = nativeErrorCode(text);
        } catch (Throwable e) {
            code = "BZ-" + (hang ? "HANG" : "CRASH") + "-" + Integer.toHexString(text.hashCode() & 0xFFFFFF).toUpperCase();
        }
        show(hang, code, text, report);
    }

    private File newest(String prefix) {
        File[] files = new File(getFilesDir(), "reports").listFiles();
        File best = null;
        if (files != null)
            for (File f : files)
                if (f.getName().startsWith(prefix) && f.getName().endsWith(".txt") && (best == null || f.lastModified() > best.lastModified()))
                    best = f;
        // Only a report of this run: an hour old is someone else's story.
        return best != null && System.currentTimeMillis() - best.lastModified() < 3_600_000 ? best : null;
    }

    private File writeHangReport() {
        File f = new File(new File(getFilesDir(), "reports"), "hang-watchdog-" + System.currentTimeMillis() + ".txt");
        f.getParentFile().mkdirs();
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write("BreezeCAD hang report\nHang: no response (seen by the watchdog)\n".getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {
        }
        return f;
    }

    static String read(File f) {
        try (FileInputStream in = new FileInputStream(f)) {   // readAllBytes is Android 13+
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            BreezeActivity.copy(in, out);
            return out.toString("UTF-8");
        } catch (Exception e) {
            return "";
        }
    }

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private void show(boolean hang, String code, String text, File report) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(20), dp(24), dp(20));

        TextView title = new TextView(this);
        title.setText(hang ? R.string.hang_title : R.string.crash_title);
        title.setTextSize(22);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        box.addView(title);

        TextView explain = new TextView(this);
        explain.setText(hang ? R.string.hang_explain : R.string.crash_explain);
        explain.setTextSize(15);
        explain.setPadding(0, dp(10), 0, dp(14));
        box.addView(explain);

        TextView codeView = new TextView(this);
        codeView.setText(code);
        codeView.setTextIsSelectable(true);
        codeView.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        codeView.setTextSize(20);
        codeView.setGravity(Gravity.CENTER_HORIZONTAL);
        codeView.setPadding(0, dp(6), 0, dp(14));
        box.addView(codeView);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button copy = new Button(this);
        copy.setText(R.string.copy_code);
        copy.setOnClickListener(v -> {
            ClipboardManager cm = getSystemService(ClipboardManager.class);
            cm.setPrimaryClip(ClipData.newPlainText("BreezeCAD", code));
        });
        row.addView(copy);
        if (report != null) {
            Button share = new Button(this);
            share.setText(R.string.share_report);
            share.setOnClickListener(v -> {
                try {
                    startActivity(ShareProvider.chooser(this, report, "text/plain", "BreezeCAD " + code));
                } catch (Exception ignored) {
                }
            });
            row.addView(share);
        }
        Button again = new Button(this);
        again.setText(R.string.restart);
        again.setOnClickListener(v -> {
            startActivity(new Intent(this, BreezeActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            finish();
        });
        row.addView(again);
        box.addView(row);

        if (!text.isEmpty()) {
            TextView details = new TextView(this);
            details.setText(text);
            details.setTextIsSelectable(true);
            details.setTypeface(Typeface.MONOSPACE);
            details.setTextSize(11);
            details.setPadding(0, dp(16), 0, 0);
            box.addView(details);
        }
        ScrollView scroll = new ScrollView(this);
        scroll.addView(box);
        setContentView(scroll);
    }
}
