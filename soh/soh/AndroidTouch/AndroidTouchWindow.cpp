#include "soh/AndroidTouch/AndroidTouchWindow.h"
#include "soh/AndroidTouch/AndroidTouch.h"

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

void AndroidTouchWindow::InitElement() {
}

void AndroidTouchWindow::DrawElement() {
}

// Replaces GuiWindow::Draw: nothing is drawn but the controls themselves.
void AndroidTouchWindow::Draw() {
    // Phones: bigger menus unless the user chose another size in the touch settings.
    static bool sScaled = false;
    if (!sScaled) {
        sScaled = true;
        const float scale = CVarGetFloat("gAndroid.UiScale", 0.0f);
        if (scale > 0.0f) {
            ImGui::GetIO().FontGlobalScale = scale;
        }
    }
    SohAndroid::HandleBackButton();
    SohAndroid::DrawTouchOverlay();
}
