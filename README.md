# ShipwrightAndroid

[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) by Harbour Masters, for Android.

This repository does not contain the game's source code. It holds a small set of patches and new files that are
applied on top of the official Ship of Harkinian code by the build workflow, so the game stays up to date with
upstream and every change made for Android is easy to read.

## What it adds

- **Android app** with a launcher: it asks for access to your files, lets you pick your Ocarina of Time ROM and
  starts the game. One APK runs on 64-bit (arm64-v8a) and 32-bit (armeabi-v7a) phones.
- **Game folder** at `/storage/emulated/0/com.shipofharkinian` with your settings, saves, mods and the extracted
  `oot.o2r`, reachable from any file manager.
- **On-screen controls**: stick, A, B, C buttons, Z, L, R and Start. A **MENU** button at the top opens the Ship of
  Harkinian menu (the Esc key on a PC) and a **TOUCH** button opens the settings where every button can be moved,
  resized and made more or less transparent.
- **Touch friendly menus**: taps work as clicks, two fingers scroll, and the menu size can be changed.
- Fixes needed to run on phones: OpenGL ES shaders, unsigned `char` on ARM, 32-bit mixer code.
- A new ROM extraction writer that no longer fails on archives with more than 65535 files.

On PC the patched game behaves exactly like Ship of Harkinian (the touch controls only exist on Android).

## Layout

| Folder | Contents |
| --- | --- |
| `patches/shipwright` | changes to the game code |
| `patches/libultraship` | changes to libultraship (game folder, shader precision) |
| `overlay` | new source files (the on-screen controls) |
| `torch-overlay` | replacement O2R writer for Torch |
| `android` | the Android app (launcher, permissions, SDL activity) |
| `scripts/apply.sh` | applies everything to a Shipwright checkout |

## Builds

Open the **Actions** tab, choose **Build Ship of Harkinian (Android port)** and press **Run workflow**. It can
build Android (both architectures in one APK), Windows and Linux. The finished files appear as artifacts:
`ShipOfHarkinian-android`, `ShipOfHarkinian-windows-x64` and `ShipOfHarkinian-linux-x86_64`.

## Using it on a phone

1. Install the APK and open it.
2. Tap **Give file access** and allow access to all files.
3. Tap **Select the ROM file** and pick your Ocarina of Time ROM.
4. The first start extracts the ROM (this takes a few minutes), then the game opens.

You need your own legally obtained ROM. No game files are included.

## Credits

Ship of Harkinian and libultraship are by [Harbour Masters](https://github.com/HarbourMasters). This repository only
adds the Android port on top of them.
