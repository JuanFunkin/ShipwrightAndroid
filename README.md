# Ship Of Harkinian By Harbour Masters For Android

Vanilla [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) (Ocarina of Time PC port) with an **Android port** applied directly to the source in this repository: touch controls, an Android app, one APK for `arm64-v8a` + `armeabi-v7a`, and the fixes needed to run on ARM/GLES.

The full source is here. Builds compile **this** tree; nothing is cloned or patched at build time, so upstream commits can never conflict with our changes.

Based on Shipwright `d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b`, libultraship `da6daf1`, Torch `2ab12fe` (all vendored as plain directories).

## What differs from upstream

| File | Change |
|---|---|
| `soh/soh/AndroidTouch/*` | New: on-screen touch controls, MENU/TOUCH buttons, touch settings window |
| `soh/src/code/padmgr.c` | Injects touch input into the controller state (`SohAndroid_InjectTouchPad`) |
| `soh/src/code/main.c` | On Android, `chdir` to the data folder |
| `soh/soh/SohGui/SohGui.cpp` | Registers the touch window (`__ANDROID__` only) |
| `soh/CMakeLists.txt` | `soh` is built as a shared library on Android |
| `soh/include/functions.h`, `soh/soh/mixer.c` | Bionic/aarch64 compile fixes |
| `soh/soh/Extractor/*` | Shows the Torch extraction error to the user |
| `libultraship/src/ship/Context.cpp` | Data path from `SOH_ANDROID_DATA_DIR` |
| `libultraship/.../gfx_opengl.cpp` | `highp` fragment precision (fixes garbled text on GLES) |
| `torch/src/archive/ZWrapper.*` | Streaming ZIP64 writer (extraction no longer fails with large archives) |
| `android/` | The Android app (launcher, activity, Gradle project) |
| `.github/workflows/build.yml` | Windows, Linux and Android builds |

Compare with upstream at the pinned commit to see the exact diff.

## Building

Run the **Build Ship of Harkinian (Android port)** workflow from the Actions tab and choose a platform (`all`, `windows`, `linux`, `android`, `android-arm64`, `android-arm32`). Artifacts:

- `ShipOfHarkinian-android` – single APK (arm64-v8a + armeabi-v7a)
- `ShipOfHarkinian-windows-x64`, `ShipOfHarkinian-linux-x86_64`

## Using it on Android

1. Install the APK and open it.
2. Grant the *all files access* permission (game data lives in `/storage/emulated/0/com.shipofharkinian`).
3. Pick your Ocarina of Time ROM; the app extracts the assets and starts the game.
4. In game: **MENU** opens the SoH menu, **TOUCH** opens the touch-controls settings.

You must provide your own legally obtained ROM.

## Credits

All the credit for Ship of Harkinian goes to the [Harbour Masters](https://github.com/HarbourMasters) team and contributors (see `docs/CREDITS.md`). This repository only adds the Android port.
