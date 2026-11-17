package com.shipofharkinian;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * First screen: copies the files the game needs to its folder (/storage/emulated/0/com.shipofharkinian), lets the
 * user pick the ROM with the system file picker and then starts the game. Every long step shows a progress bar
 * with a percentage.
 */
public class LauncherActivity extends Activity {
    // Bump to re-copy the bundled files after an update.
    private static final int ASSET_VERSION = 1;

    private static final int BG_TOP = Color.rgb(12, 18, 30);
    private static final int BG_BOTTOM = Color.rgb(30, 22, 52);
    private static final int CARD = Color.argb(200, 22, 28, 44);
    private static final int ACCENT = Color.rgb(124, 255, 155);
    private static final int ACCENT_DARK = Color.rgb(46, 160, 92);
    private static final int TEXT = Color.rgb(232, 236, 244);
    private static final int TEXT_DIM = Color.rgb(150, 158, 176);

    private File dataDir;
    private TextView status;
    private TextView percent;
    private Button permissionButton;
    private ProgressBar progress;
    private boolean prepared = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        dataDir = DataDir.dir();
        buildUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!DataDir.hasAccess(this)) {
            showPermissionStep();
            return;
        }
        if (prepared) {
            return;
        }
        prepared = true;
        permissionButton.setVisibility(View.GONE);
        showProgress("Preparing files...", 0);
        new Thread(() -> {
            dataDir.mkdirs();
            new File(dataDir, "tmp").mkdirs(); // temp folder the ROM extraction needs (Android has none)
            try {
                copyBundledFiles();
            } catch (IOException e) {
                runOnUiThread(() -> {
                    showMessage("Could not prepare the game files:\n" + e.getMessage());
                    prepared = false;
                });
                return;
            }
            runOnUiThread(this::refresh);
        }).start();
    }

    // ---- UI ------------------------------------------------------------------------------------

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private static GradientDrawable rounded(int color, int radiusPx) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(radiusPx);
        return d;
    }

    private Button makeButton(String text, boolean primary) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(17);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setStateListAnimator(null);
        if (primary) {
            GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                    new int[] {ACCENT_DARK, ACCENT});
            d.setCornerRadius(dp(28));
            b.setBackground(d);
            b.setTextColor(Color.rgb(8, 30, 18));
        } else {
            GradientDrawable d = rounded(Color.argb(40, 255, 255, 255), dp(28));
            d.setStroke(dp(1), Color.argb(120, 255, 255, 255));
            b.setBackground(d);
            b.setTextColor(TEXT);
        }
        LinearLayout.LayoutParams lp =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(56));
        lp.topMargin = dp(12);
        b.setLayoutParams(lp);
        b.setVisibility(View.GONE);
        return b;
    }

    private void buildUi() {
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] {BG_TOP, BG_BOTTOM});

        ScrollView scroll = new ScrollView(this);
        scroll.setBackground(bg);
        scroll.setFillViewport(true);

        LinearLayout outer = new LinearLayout(this);
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.setGravity(Gravity.CENTER);
        outer.setPadding(dp(20), dp(16), dp(20), dp(16));
        scroll.addView(outer, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        ImageView logo = new ImageView(this);
        logo.setImageResource(R.mipmap.ic_launcher);
        logo.setBackground(rounded(Color.rgb(27, 58, 42), dp(24)));
        logo.setClipToOutline(true);
        outer.addView(logo, new LinearLayout.LayoutParams(dp(72), dp(72)));

        TextView title = new TextView(this);
        title.setText("Ship of Harkinian");
        title.setTextSize(30);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, dp(10), 0, 0);
        outer.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("Starting up...");
        subtitle.setTextSize(14);
        subtitle.setTextColor(ACCENT);
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setPadding(0, 0, 0, dp(16));
        outer.addView(subtitle);

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER_HORIZONTAL);
        card.setBackground(rounded(CARD, dp(24)));
        card.setPadding(dp(22), dp(20), dp(22), dp(22));
        outer.addView(card, new LinearLayout.LayoutParams(dp(460), ViewGroup.LayoutParams.WRAP_CONTENT));

        status = new TextView(this);
        status.setTextSize(16);
        status.setTextColor(TEXT);
        status.setGravity(Gravity.CENTER);
        status.setLineSpacing(0, 1.15f);
        card.addView(status);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        GradientDrawable track = rounded(Color.argb(70, 255, 255, 255), dp(6));
        GradientDrawable fill = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                new int[] {ACCENT_DARK, ACCENT});
        fill.setCornerRadius(dp(6));
        LayerDrawable layers = new LayerDrawable(new android.graphics.drawable.Drawable[] {
                track, new ClipDrawable(fill, Gravity.START, ClipDrawable.HORIZONTAL)});
        layers.setId(0, android.R.id.background);
        layers.setId(1, android.R.id.progress);
        progress.setProgressDrawable(layers);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams plp =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(12));
        plp.topMargin = dp(16);
        card.addView(progress, plp);

        percent = new TextView(this);
        percent.setTextSize(15);
        percent.setTypeface(Typeface.DEFAULT_BOLD);
        percent.setTextColor(ACCENT);
        percent.setGravity(Gravity.CENTER);
        percent.setPadding(0, dp(6), 0, 0);
        percent.setVisibility(View.GONE);
        card.addView(percent);

        permissionButton = makeButton("Give file access", true);
        permissionButton.setOnClickListener(v -> DataDir.requestAccess(this, 2));
        card.addView(permissionButton);


        setContentView(scroll);
    }

    private void hideButtons() {
        permissionButton.setVisibility(View.GONE);
    }

    /** fraction < 0 shows an indeterminate bar (unknown length). */
    private void showProgress(String text, int permille) {
        hideButtons();
        status.setText(text);
        progress.setVisibility(View.VISIBLE);
        percent.setVisibility(View.VISIBLE);
        if (permille < 0) {
            progress.setIndeterminate(true);
            percent.setText("");
        } else {
            progress.setIndeterminate(false);
            progress.setProgress(permille);
            percent.setText((permille / 10) + "%");
        }
    }

    private void showMessage(String text) {
        progress.setVisibility(View.GONE);
        percent.setVisibility(View.GONE);
        status.setText(text);
    }

    private void showPermissionStep() {
        hideButtons();
        progress.setVisibility(View.GONE);
        percent.setVisibility(View.GONE);
        permissionButton.setVisibility(View.VISIBLE);
        status.setText("Ship of Harkinian keeps its files (ROM, settings, mods, textures and mods) in\n"
                + "/storage/emulated/0/com.shipofharkinian\n"
                + "so you can reach them from any file manager.\n\n"
                + "Tap the button and allow access to all files.");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        // onResume runs right after and checks the permission again
    }

    // ---- flow ----------------------------------------------------------------------------------

    /** Files are ready: open the game, whose own menu asks for the ROM (like on PC). */
    private void refresh() {
        startGame();
    }

    private void startGame() {
        startActivity(new Intent(this, SohActivity.class));
        finish();
    }

    // ---- bundled files (soh.o2r and the extractor's assets folder) ------------------------------

    private long copiedBytes;
    private long totalBytes;
    private int lastPermille = -1;

    private void copyBundledFiles() throws IOException {
        SharedPreferences prefs = getSharedPreferences("files", MODE_PRIVATE);
        if (prefs.getInt("assetVersion", 0) == ASSET_VERSION && new File(dataDir, "soh.o2r").exists()) {
            return;
        }
        AssetManager assets = getAssets();

        // Work out the total size first so the bar is real.
        List<String> files = new ArrayList<>();
        listAssetFiles(assets, "extractor", files);
        totalBytes = assetSize(assets, "soh.o2r");
        for (String f : files) {
            totalBytes += assetSize(assets, f);
        }
        copiedBytes = 0;
        lastPermille = -1;

        copyAssetFile(assets, "soh.o2r", new File(dataDir, "soh.o2r"));
        for (String f : files) {
            copyAssetFile(assets, f, new File(dataDir, "assets/" + f.substring("extractor/".length())));
        }
        prefs.edit().putInt("assetVersion", ASSET_VERSION).apply();
    }

    private void listAssetFiles(AssetManager assets, String path, List<String> out) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            out.add(path);
            return;
        }
        for (String child : children) {
            listAssetFiles(assets, path + "/" + child, out);
        }
    }

    private long assetSize(AssetManager assets, String path) {
        try (InputStream in = assets.open(path)) {
            return in.available();
        } catch (IOException e) {
            return 0;
        }
    }

    private void copyAssetFile(AssetManager assets, String path, File target) throws IOException {
        File parent = target.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[1 << 16];
            int n;
            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
                copiedBytes += n;
                if (totalBytes > 0) {
                    int p = (int) Math.min(1000, copiedBytes * 1000 / totalBytes);
                    if (p != lastPermille) {
                        lastPermille = p;
                        final int shown = p;
                        runOnUiThread(() -> showProgress("Preparing files...", shown));
                    }
                }
            }
        }
    }
}
