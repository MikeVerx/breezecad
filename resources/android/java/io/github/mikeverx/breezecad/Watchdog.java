// The crash and hang watchdog (docs/ADR/0004-mobile-touch-ui.md, "Сбои и
// зависания на Android", layer 2): a service in its own process (":crash")
// that watches the app's process.
//
// The app (src/platform/Android.cpp) keeps two files in files/watchdog:
//   state — "fg <pid>", "bg <pid>" or "exit": in front, in the background,
//           ended normally;
//   beat  — rewritten about once a second by the app's loop.
// The app's process gone while the state says "fg" → it crashed: a notice
// "BreezeCAD закрылся из-за ошибки" opens the error screen (CrashActivity).
// No beat for a while in front → it hangs: a notice with "Подождать" and
// "Закрыть и отправить отчёт".
//
// A notice is the fast way, not the only one: notifications may be off and
// the system may stop this service. The app checks the last run itself at
// the next start (layer 3).
package io.github.mikeverx.breezecad;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;

import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;

public class Watchdog extends Service {
    static final String CHANNEL = "problems";
    static final int CRASH_NOTICE = 1, HANG_NOTICE = 2;
    static final long HANG_MS = 10_000;

    private volatile boolean running;
    private Thread thread;

    /// Started (again) whenever the app comes to the front: the system stops
    /// background services after a while.
    static void start(Context c) {
        try {
            c.startService(new Intent(c, Watchdog.class));
        } catch (Exception ignored) {
            // Not allowed now (the app is not in front): the next start tries again.
        }
    }

    static File dir(Context c) { return new File(c.getFilesDir(), "watchdog"); }

    static String read(File f) {
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] b = new byte[256];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8).trim() : "";
        } catch (Exception e) {
            return "";
        }
    }

    static void setForeground(Context c, boolean front) {
        // The app writes its state itself (it knows its pid and its exit);
        // here only the service is kept alive while the app is in front.
        if (front) start(c);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (!running) {
            running = true;
            thread = new Thread(this::watch, "BreezeCAD watchdog");
            thread.start();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        if (thread != null) thread.interrupt();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    private void watch() {
        File state = new File(dir(this), "state"), beat = new File(dir(this), "beat");
        boolean hangShown = false;
        while (running) {
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {
                return;
            }
            String[] s = read(state).split(" ");
            if (s.length < 2) continue;              // "exit", or not started yet
            int pid;
            try {
                pid = Integer.parseInt(s[1]);
            } catch (NumberFormatException e) {
                continue;
            }
            boolean alive = new File("/proc/" + pid).exists();
            if (!alive) {
                if (s[0].equals("fg")) notifyProblem(false, pid);
                writeState(state, "gone " + pid);
                stopSelf();
                return;
            }
            boolean stale = s[0].equals("fg") && System.currentTimeMillis() - beat.lastModified() > HANG_MS;
            if (stale && !hangShown) {
                notifyProblem(true, pid);
                hangShown = true;
            } else if (!stale && hangShown) {
                cancel(HANG_NOTICE);
                hangShown = false;
            }
        }
    }

    static void writeState(Context c, String text) {
        File d = dir(c);
        d.mkdirs();
        writeState(new File(d, "state"), text);
        writeState(new File(d, "beat"), "start");   // a fresh beat: the program needs a few seconds before its own
    }

    private static void writeState(File state, String text) {
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(state)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {
        }
    }

    private void channel() {
        if (Build.VERSION.SDK_INT < 26) return;
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm.getNotificationChannel(CHANNEL) == null)
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, getString(R.string.channel_problems),
                    NotificationManager.IMPORTANCE_HIGH));
    }

    private void cancel(int id) {
        getSystemService(NotificationManager.class).cancel(id);
    }

    private void notifyProblem(boolean hang, int pid) {
        channel();
        Intent open = new Intent(this, CrashActivity.class)
                .putExtra(CrashActivity.EXTRA_HANG, hang)
                .putExtra(CrashActivity.EXTRA_PID, pid)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        int immutable = Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0;
        PendingIntent show = PendingIntent.getActivity(this, hang ? 2 : 1, open, PendingIntent.FLAG_UPDATE_CURRENT | immutable);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        b.setSmallIcon(R.drawable.ic_notice)
         .setContentTitle(getString(hang ? R.string.hang_title : R.string.crash_title))
         .setContentText(getString(hang ? R.string.hang_text : R.string.crash_text))
         .setAutoCancel(true)
         .setContentIntent(show);
        if (hang) {
            Intent wait = new Intent(this, CrashActivity.class).setAction(CrashActivity.ACTION_WAIT)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            b.addAction(new Notification.Action.Builder(null, getString(R.string.hang_wait),
                    PendingIntent.getActivity(this, 3, wait, PendingIntent.FLAG_UPDATE_CURRENT | immutable)).build());
            b.addAction(new Notification.Action.Builder(null, getString(R.string.hang_close), show).build());
        }
        try {
            getSystemService(NotificationManager.class).notify(hang ? HANG_NOTICE : CRASH_NOTICE, b.build());
        } catch (SecurityException ignored) {
            // No permission for notices (Android 13+): the next start shows the report.
        }
    }
}
