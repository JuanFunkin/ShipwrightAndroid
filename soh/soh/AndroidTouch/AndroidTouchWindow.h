#pragma once

#include <ship/window/gui/GuiWindow.h>

// Invisible window whose only job is to run the touch overlay once per frame (Android only).
class AndroidTouchWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override{};
    void Draw() override;
};
