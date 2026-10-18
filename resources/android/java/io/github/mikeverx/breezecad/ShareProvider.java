// Hands a file to another app for the system's "share" sheet (reports,
// exports) — a minimal FileProvider without the support library: only files
// copied into cache/share are served, read-only, for the grant in the intent.
package io.github.mikeverx.breezecad;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;

public class ShareProvider extends ContentProvider {
    static String authority(Context c) { return c.getPackageName() + ".share"; }

    static File folder(Context c) { return new File(c.getCacheDir(), "share"); }

    /// A chooser for `file`: copied into the shared folder, then offered.
    static Intent chooser(Context c, File file, String mime, String subject) throws java.io.IOException {
        File dir = folder(c);
        dir.mkdirs();
        File copy = new File(dir, file.getName());
        if (!copy.getCanonicalPath().equals(file.getCanonicalPath()))
            try (FileInputStream in = new FileInputStream(file); FileOutputStream out = new FileOutputStream(copy)) {
                BreezeActivity.copy(in, out);
            }
        Uri uri = new Uri.Builder().scheme("content").authority(authority(c)).appendPath(copy.getName()).build();
        Intent send = new Intent(Intent.ACTION_SEND)
                .setType(mime)
                .putExtra(Intent.EXTRA_STREAM, uri)
                .putExtra(Intent.EXTRA_SUBJECT, subject)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        send.setClipData(android.content.ClipData.newRawUri(copy.getName(), uri));
        return Intent.createChooser(send, subject).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    }

    private File fileOf(Uri uri) throws FileNotFoundException {
        String name = uri.getLastPathSegment();
        if (name == null || name.contains("/") || name.startsWith(".")) throw new FileNotFoundException();
        File f = new File(folder(getContext()), name);
        if (!f.isFile()) throw new FileNotFoundException(name);
        return f;
    }

    @Override public boolean onCreate() { return true; }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(fileOf(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
        try {
            File f = fileOf(uri);
            MatrixCursor c = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
            c.addRow(new Object[]{f.getName(), f.length()});
            return c;
        } catch (FileNotFoundException e) {
            return null;
        }
    }

    @Override
    public String getType(Uri uri) {
        String name = uri.getLastPathSegment();
        if (name == null) return null;
        if (name.endsWith(".txt") || name.endsWith(".log")) return "text/plain";
        if (name.endsWith(".zip")) return "application/zip";
        if (name.endsWith(".stl")) return "model/stl";
        if (name.endsWith(".3mf")) return "model/3mf";
        if (name.endsWith(".step") || name.endsWith(".stp")) return "model/step";
        return "application/octet-stream";
    }

    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] args) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) { return 0; }
}
