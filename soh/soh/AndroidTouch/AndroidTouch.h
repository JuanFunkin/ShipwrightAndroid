#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Called by padmgr.c each frame: adds the on-screen controller (Android) to the game's pad data (OSContPad*). */
void SohAndroid_InjectTouchPad(void* pad);

#ifdef __cplusplus
}

namespace SohAndroid {
void HandleBackButton();  // every frame: the phone's back button toggles the SoH menu
void DrawTouchOverlay();  // every frame: reads fingers, draws the controls and the MENU / TOUCH buttons
void DrawTouchSettings(); // the settings window (opened by the TOUCH button)
void DrawTouchTab();      // the settings' contents
} // namespace SohAndroid
#endif
