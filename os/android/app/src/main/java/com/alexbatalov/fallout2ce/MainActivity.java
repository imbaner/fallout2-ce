package com.alexbatalov.fallout2ce;

import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.view.DisplayCutout;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

public class MainActivity extends SDLActivity {
    private boolean noExit = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        final File externalFilesDir = getExternalFilesDir(null);
        // A replacement of the game's files cut short (the app died while
        // moving them in): finished before the game reads them.
        if (externalFilesDir != null && GameFiles.isCommitPending(externalFilesDir)) {
            try {
                GameFiles.finish(externalFilesDir);
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        // The same for an import of saves.
        if (externalFilesDir != null && GameSaves.isCommitPending(externalFilesDir)) {
            try {
                GameSaves.finish(externalFilesDir);
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        installBundledFiles(externalFilesDir);

        super.onCreate(savedInstanceState);

        // Without the game's files (or copied in part): the import screen.
        if (!GameImport.isInstalled(externalFilesDir)) {
            final Intent intent = new Intent(this, ImportActivity.class);
            startActivity(intent);

            noExit = true;
            finish();
        }
    }

    // The engine's own data (ce.dat, a folder in the app's assets, see
    // app/build.gradle) next to the game's, when the app is new or updated.
    private void installBundledFiles(File target) {
        if (target == null) {
            return;
        }

        String stamp;
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            stamp = info.versionName + " " + info.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            return;
        }

        File ceDat = new File(target, "ce.dat");
        File stampFile = new File(ceDat, ".bundled");
        if (stampFile.isFile() && stamp.equals(readText(stampFile))) {
            return;
        }

        deleteRecursively(ceDat);
        try {
            copyAssets("ce.dat", ceDat);
            try (OutputStream out = new FileOutputStream(stampFile)) {
                out.write(stamp.getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void copyAssets(String path, File target) throws IOException {
        AssetManager assets = getAssets();
        String[] names = assets.list(path);
        if (names == null || names.length == 0) {
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[65536];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                }
            }
            return;
        }

        if (!target.isDirectory() && !target.mkdirs()) {
            throw new IOException("can't make " + target);
        }
        for (String name : names) {
            copyAssets(path + "/" + name, new File(target, name));
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    private static String readText(File file) {
        try (InputStream in = new FileInputStream(file)) {
            byte[] data = new byte[(int) file.length()];
            int read = in.read(data);
            return new String(data, 0, Math.max(read, 0), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        if (!noExit) {
            // Needed to make sure libc calls exit handlers, which releases
            // in-game resources.
            System.exit(0);
        }
    }

    // Called from native code (hud_layout.cc): display cutout safe insets in
    // pixels - left, top, right, bottom.
    public static int[] getSafeInsets() {
        final int[] insets = new int[4];
        if (Build.VERSION.SDK_INT >= 28 && mSingleton != null) {
            final WindowInsets windowInsets = mSingleton.getWindow().getDecorView().getRootWindowInsets();
            if (windowInsets != null) {
                final DisplayCutout cutout = windowInsets.getDisplayCutout();
                if (cutout != null) {
                    insets[0] = cutout.getSafeInsetLeft();
                    insets[1] = cutout.getSafeInsetTop();
                    insets[2] = cutout.getSafeInsetRight();
                    insets[3] = cutout.getSafeInsetBottom();
                }
            }
        }
        return insets;
    }

    // Called from native code (mui_preferences.cc, "Game files"): the app's
    // import screen for [mode] (ImportActivity.MODE), in its own process.
    // The saves' import and export go over the game (paused behind it, it
    // reads the saves again when back); replacing the game ([mode] null)
    // closes it (its process exits, see onDestroy) and the import screen
    // starts it again when done.
    public void openGameFiles(String mode) {
        runOnUiThread(() -> {
            Intent intent = new Intent(this, ImportActivity.class);
            if (mode != null) {
                intent.putExtra(ImportActivity.MODE, mode);
            }
            startActivity(intent);
            if (mode == null) {
                finish();
            }
        });
    }

    // Debug builds: the game's command line from the launch intent, for
    // automated tests on a device:
    // adb shell am start -n <package>/.MainActivity --es args "--dev-autotest=<dir> ..."
    @Override
    protected String[] getArguments() {
        // Not when opened again from recent apps: that intent is the old one.
        final boolean fromHistory = (getIntent().getFlags() & Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0;
        final String args = BuildConfig.DEBUG && !fromHistory ? getIntent().getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty()) {
            return new String[0];
        }
        return args.trim().split("\\s+");
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{
            "fallout2-ce",
        };
    }
}
