package com.shipofharkinian;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.OpenableColumns;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/** The game itself: SDL creates the window and runs libsoh.so. */
public class SohActivity extends SDLActivity {
    private static final int PICK_ROM = 7001;

    // ROM picker used by the game's own extraction menu (called from native code, see Extract.cpp).
    // state: 0 = idle, 1 = waiting, 2 = ROM copied, 3 = cancelled / failed
    private static volatile int sPickState = 0;
    private static volatile String sPickPath = "";

    private static native void nativeBackPressed();

    /** Native: opens the system file picker. */
    public void pickRomFromNative() {
        sPickState = 1;
        sPickPath = "";
        runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                startActivityForResult(intent, PICK_ROM);
            } catch (Exception e) {
                sPickState = 3;
            }
        });
    }

    /** Native polls this. */
    public int pickRomState() {
        return sPickState;
    }

    public String pickRomPath() {
        return sPickPath;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            sPickState = 3;
            return;
        }
        final Uri uri = data.getData();
        new Thread(() -> {
            try {
                File dir = DataDir.dir();
                dir.mkdirs();
                File target = new File(dir, "rom.z64");
                try (InputStream in = getContentResolver().openInputStream(uri);
                     OutputStream out = new FileOutputStream(target)) {
                    byte[] buffer = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                    }
                }
                sPickPath = target.getAbsolutePath();
                sPickState = 2;
            } catch (Exception e) {
                sPickState = 3;
            }
        }).start();
    }

    /** The back button (triangle / back gesture) opens and closes the SoH menu instead of closing the game. */
    @Override
    public void onBackPressed() {
        try {
            nativeBackPressed();
        } catch (UnsatisfiedLinkError ignored) {
        }
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        DataDir.exportToNative(); // before SDL starts the game
        useWholeScreen(getWindow());
        super.onCreate(savedInstanceState);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    /** Draw under the notch / camera cut-out instead of leaving a black border. */
    private static void useWholeScreen(Window window) {
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = window.getAttributes();
            lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                    ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                    : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(lp);
        }
    }

    /** Immersive mode: no status bar and no navigation buttons (a swipe from the edge shows them briefly). */
    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "soh" };
    }
}
