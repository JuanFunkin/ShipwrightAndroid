#include "soh/AndroidTouch/AndroidTouch.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include "soh/cvar_prefixes.h"

extern "C" {
#include <libultraship/libultra/controller.h>
}

/*
 * On-screen controller for phones and tablets.
 *
 * The overlay draws the buttons and reads the fingers directly from SDL (so several can be used at once). The
 * result is written straight into the game's controller data (SohAndroid_InjectTouchPad, called by padmgr.c
 * every frame), so it works without any gamepad being detected.
 *
 *  - The stick is floating: it appears where the left thumb lands and follows it, so it is never out of reach.
 *  - The MENU button opens the SoH menu (the phone's back button does the same). It hides while the menu is open.
 *  - Every control (MENU included) can be moved, resized and given its own opacity: SoH menu > Settings >
 *    Touch Controls > Edit layout.
 *
 * It is on by default on Android. On other systems set SOH_TOUCH=1 to try it.
 */

namespace {

enum Kind { kStick, kRound, kArrow, kPill };
enum Dir { kUp, kDown, kLeft, kRight };

// N64 controller bits as the game reads them (same values as A_BUTTON, B_BUTTON, ... in libultra).
constexpr uint16_t kBtnA = 0x8000, kBtnB = 0x4000, kBtnZ = 0x2000, kBtnStart = 0x1000, kBtnL = 0x0020,
                   kBtnR = 0x0010, kBtnCUp = 0x0008, kBtnCDown = 0x0004, kBtnCLeft = 0x0002, kBtnCRight = 0x0001;

struct Control {
    const char* id;    // used in the saved settings
    const char* label;
    Kind kind;
    float x, y;        // default center, fraction of the screen
    float r;           // default size, fraction of the screen height
    uint16_t bit;      // N64 button bit (0 = none)
    ImU32 color;
    Dir dir;           // arrows only
    float wide;        // pills only: half width in units of r
};

Control sControls[] = {
    { "stick", "Stick", kStick, 0.14f, 0.68f, 0.15f, 0, IM_COL32(255, 255, 255, 255), kUp, 0 },
    { "a", "A", kRound, 0.91f, 0.73f, 0.090f, kBtnA, IM_COL32(60, 210, 120, 255), kUp, 0 },
    { "b", "B", kRound, 0.80f, 0.85f, 0.068f, kBtnB, IM_COL32(240, 90, 80, 255), kUp, 0 },
    { "cup", "C Up", kArrow, 0.87f, 0.26f, 0.050f, kBtnCUp, IM_COL32(245, 205, 70, 255), kUp, 0 },
    { "cdown", "C Down", kArrow, 0.87f, 0.46f, 0.050f, kBtnCDown, IM_COL32(245, 205, 70, 255), kDown, 0 },
    { "cleft", "C Left", kArrow, 0.80f, 0.36f, 0.050f, kBtnCLeft, IM_COL32(245, 205, 70, 255), kLeft, 0 },
    { "cright", "C Right", kArrow, 0.94f, 0.36f, 0.050f, kBtnCRight, IM_COL32(245, 205, 70, 255), kRight, 0 },
    { "z", "Z", kPill, 0.09f, 0.30f, 0.050f, kBtnZ, IM_COL32(170, 180, 255, 255), kUp, 1.5f },
    { "l", "L", kPill, 0.09f, 0.14f, 0.045f, kBtnL, IM_COL32(170, 180, 255, 255), kUp, 1.5f },
    { "r", "R", kPill, 0.91f, 0.10f, 0.050f, kBtnR, IM_COL32(170, 180, 255, 255), kUp, 1.5f },
    { "start", "START", kPill, 0.50f, 0.93f, 0.040f, kBtnStart, IM_COL32(230, 230, 240, 255), kUp, 2.0f },
    { "menu", "MENU", kPill, 0.50f, 0.065f, 0.040f, 0, IM_COL32(255, 255, 255, 255), kUp, 2.1f },
};
constexpr int kControlCount = sizeof(sControls) / sizeof(sControls[0]);
constexpr int kStickIndex = 0;
constexpr int kMenuIndex = kControlCount - 1;

std::mutex sFingerMutex;
std::map<SDL_FingerID, ImVec2> sFingers; // current position, normalized 0..1
std::map<SDL_FingerID, ImVec2> sStarts;  // where each finger first touched, normalized
// What the game reads this frame (written by the overlay, read by padmgr.c).
std::atomic<uint16_t> sPadButtons{ 0 };
std::atomic<int> sPadStickX{ 0 };
std::atomic<int> sPadStickY{ 0 };
std::atomic<bool> sBackPressed{ false };
bool sWatching = false;
bool sEdit = false;
int sSelected = 1;
SDL_FingerID sDragFinger = -1;
ImVec2 sDragOffset(0, 0);
bool sDragging = false;
float sScrollPrevY = -1.0f;
bool sStickActive = false;
SDL_FingerID sStickFinger = -1;
ImVec2 sStickBase(0, 0); // pixels
bool sMenuWasDown = false;

std::string Key(const Control& c, const char* field) {
    return std::string("gAndroid.Touch.") + c.id + "." + field;
}

float PosX(const Control& c) { return CVarGetFloat(Key(c, "X").c_str(), c.x); }
float PosY(const Control& c) { return CVarGetFloat(Key(c, "Y").c_str(), c.y); }
float CtrlSize(const Control& c) { return CVarGetFloat(Key(c, "R").c_str(), c.r); }
float CtrlOpacity(const Control& c) { return CVarGetFloat(Key(c, "O").c_str(), 1.0f); }

float Scale() { return CVarGetFloat("gAndroid.Touch.Scale", 1.0f); }
float Opacity() { return CVarGetFloat("gAndroid.Touch.Opacity", 0.6f); }

bool Enabled() {
#ifdef __ANDROID__
    return CVarGetInteger("gAndroid.Touch.Enabled", 1) != 0;
#else
    return std::getenv("SOH_TOUCH") != nullptr && CVarGetInteger("gAndroid.Touch.Enabled", 1) != 0;
#endif
}

bool FloatingStick() { return CVarGetInteger("gAndroid.Touch.FloatingStick", 1) != 0; }
bool ShowMenuButton() { return CVarGetInteger("gAndroid.Touch.ShowMenuButton", 1) != 0; }

bool SohMenuVisible() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    return gui != nullptr && gui->GetMenu() != nullptr && gui->GetMenu()->IsVisible();
}

// The SoH menu is a GuiWindow: it has to be toggled through the window, changing its saved flag does nothing.
void SetSohMenuVisible(bool visible) {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui != nullptr && gui->GetMenu() != nullptr && gui->GetMenu()->IsVisible() != visible) {
        gui->GetMenu()->ToggleVisibility();
    }
}

int EventWatch(void*, SDL_Event* event) {
    if (event->type == SDL_FINGERDOWN) {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        const ImVec2 p(event->tfinger.x, event->tfinger.y);
        sFingers[event->tfinger.fingerId] = p;
        sStarts[event->tfinger.fingerId] = p;
    } else if (event->type == SDL_FINGERMOTION) {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        sFingers[event->tfinger.fingerId] = ImVec2(event->tfinger.x, event->tfinger.y);
    } else if (event->type == SDL_FINGERUP) {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        sFingers.erase(event->tfinger.fingerId);
        sStarts.erase(event->tfinger.fingerId);
    }
    return 0;
}

void EnsureWatch() {
    if (!sWatching) {
        sWatching = true;
        SDL_AddEventWatch(EventWatch, nullptr);
    }
}

struct Finger {
    SDL_FingerID id;
    ImVec2 pos;   // pixels
    ImVec2 start; // pixels
};

// Screen geometry of a control, in pixels.
struct Geo {
    ImVec2 center;
    float size;     // radius for round controls, half height for pills
    float halfW;    // half width (pills) or radius
};

Geo Place(const Control& c, ImVec2 screen) {
    const float s = CtrlSize(c) * Scale() * screen.y;
    return { ImVec2(PosX(c) * screen.x, PosY(c) * screen.y), s, c.kind == kPill ? s * c.wide : s };
}

float Dist(ImVec2 a, ImVec2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// Is the point on the control? "grow" makes the target a little bigger than what is drawn (fingers are fat).
bool Hit(const Control& c, const Geo& g, ImVec2 p, float grow) {
    if (c.kind == kPill) {
        const float hh = g.size * 0.85f * grow;
        return std::fabs(p.x - g.center.x) <= g.halfW * grow && std::fabs(p.y - g.center.y) <= hh;
    }
    return Dist(p, g.center) <= g.size * grow;
}

ImU32 WithAlpha(ImU32 color, float alpha) {
    const int a = std::clamp(static_cast<int>(alpha * 255.0f), 0, 255);
    return (color & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24);
}

void CenteredText(ImDrawList* draw, ImVec2 center, float fontSize, ImU32 color, const char* text) {
    ImFont* font = ImGui::GetFont();
    const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    draw->AddText(font, fontSize, ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), color, text);
}

void DrawArrow(ImDrawList* draw, ImVec2 c, float r, Dir dir, ImU32 color) {
    const float k = r * 0.5f;
    ImVec2 a, b, d;
    switch (dir) {
        case kUp: a = ImVec2(c.x, c.y - k); b = ImVec2(c.x - k, c.y + k * 0.7f); d = ImVec2(c.x + k, c.y + k * 0.7f); break;
        case kDown: a = ImVec2(c.x, c.y + k); b = ImVec2(c.x + k, c.y - k * 0.7f); d = ImVec2(c.x - k, c.y - k * 0.7f); break;
        case kLeft: a = ImVec2(c.x - k, c.y); b = ImVec2(c.x + k * 0.7f, c.y + k); d = ImVec2(c.x + k * 0.7f, c.y - k); break;
        default: a = ImVec2(c.x + k, c.y); b = ImVec2(c.x - k * 0.7f, c.y - k); d = ImVec2(c.x - k * 0.7f, c.y + k); break;
    }
    draw->AddTriangleFilled(a, b, d, color);
}

// Flat, translucent style: dark glass with a coloured rim; it lights up while pressed.
void DrawControl(ImDrawList* draw, const Control& c, const Geo& g, bool pressed, float alpha, bool highlight) {
    const float rim = std::max(2.0f, g.size * 0.07f);
    const ImU32 glass = IM_COL32(12, 14, 24, 255);
    const ImU32 fill = pressed ? WithAlpha(c.color, 0.85f * alpha) : WithAlpha(glass, 0.55f * alpha);
    const ImU32 ring = WithAlpha(c.color, (pressed ? 1.0f : 0.85f) * alpha);
    const ImU32 text = pressed ? WithAlpha(IM_COL32(10, 10, 20, 255), alpha) : WithAlpha(IM_COL32(255, 255, 255, 255), alpha);

    if (c.kind == kPill) {
        const float hh = g.size * 0.85f;
        const ImVec2 mn(g.center.x - g.halfW, g.center.y - hh), mx(g.center.x + g.halfW, g.center.y + hh);
        draw->AddRectFilled(mn, mx, fill, hh);
        draw->AddRect(mn, mx, ring, hh, 0, rim);
        if (highlight) {
            draw->AddRect(ImVec2(mn.x - 5, mn.y - 5), ImVec2(mx.x + 5, mx.y + 5), IM_COL32(255, 255, 255, 230), hh + 5, 0, 3.0f);
        }
        CenteredText(draw, g.center, g.size * 0.85f, text, c.label);
        return;
    }
    const float radius = pressed ? g.size * 0.93f : g.size;
    draw->AddCircleFilled(g.center, radius, fill, 48);
    draw->AddCircle(g.center, radius, ring, 48, rim);
    if (highlight) {
        draw->AddCircle(g.center, radius + 6.0f, IM_COL32(255, 255, 255, 230), 48, 3.0f);
    }
    if (c.kind == kArrow) {
        DrawArrow(draw, g.center, radius, c.dir, pressed ? text : ring);
    } else {
        CenteredText(draw, g.center, radius * 1.05f, text, c.label);
    }
}

void DrawStick(ImDrawList* draw, ImVec2 base, ImVec2 knob, float radius, float alpha, bool active, bool highlight) {
    const float a = active ? alpha : alpha * 0.55f;
    draw->AddCircleFilled(base, radius, WithAlpha(IM_COL32(255, 255, 255, 255), 0.10f * a), 64);
    draw->AddCircle(base, radius, WithAlpha(IM_COL32(255, 255, 255, 255), 0.55f * a), 64, std::max(2.0f, radius * 0.03f));
    draw->AddCircle(base, radius * 0.55f, WithAlpha(IM_COL32(255, 255, 255, 255), 0.18f * a), 48, 1.5f);
    // Four small direction marks.
    const float m = radius * 0.80f, t = radius * 0.08f;
    const ImU32 mark = WithAlpha(IM_COL32(255, 255, 255, 255), 0.45f * a);
    draw->AddTriangleFilled(ImVec2(base.x, base.y - m - t), ImVec2(base.x - t, base.y - m + t), ImVec2(base.x + t, base.y - m + t), mark);
    draw->AddTriangleFilled(ImVec2(base.x, base.y + m + t), ImVec2(base.x + t, base.y + m - t), ImVec2(base.x - t, base.y + m - t), mark);
    draw->AddTriangleFilled(ImVec2(base.x - m - t, base.y), ImVec2(base.x - m + t, base.y + t), ImVec2(base.x - m + t, base.y - t), mark);
    draw->AddTriangleFilled(ImVec2(base.x + m + t, base.y), ImVec2(base.x + m - t, base.y - t), ImVec2(base.x + m - t, base.y + t), mark);
    // The knob.
    draw->AddCircleFilled(knob, radius * 0.38f, WithAlpha(IM_COL32(235, 240, 255, 255), (active ? 0.65f : 0.35f) * alpha), 48);
    draw->AddCircle(knob, radius * 0.38f, WithAlpha(IM_COL32(255, 255, 255, 255), 0.9f * a), 48, std::max(2.0f, radius * 0.03f));
    if (highlight) {
        draw->AddCircle(base, radius + 6.0f, IM_COL32(255, 255, 255, 230), 64, 3.0f);
    }
}

} // namespace

namespace SohAndroid {

// Android's back button (the triangle / back gesture) opens and closes the SoH menu.
void HandleBackButton() {
    if (sBackPressed.exchange(false)) {
        if (sEdit) {
            sEdit = false;
            SetSohMenuVisible(true);
        } else {
            SetSohMenuVisible(!SohMenuVisible());
        }
    }
}

static void DrawEditPanel(ImVec2 screen) {
    Control& c = sControls[sSelected];
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y - 10.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(screen.x * 0.46f, 0), ImGuiCond_Always);
    ImGui::Begin("Edit touch layout", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    ImGui::TextWrapped("Drag a control to move it. Selected: %s", c.label[0] != '\0' ? c.label : c.id);
    float r = CtrlSize(c);
    const float lo = c.kind == kStick ? 0.06f : 0.02f, hi = c.kind == kStick ? 0.30f : 0.16f;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##size", &r, lo, hi, "Size %.3f")) {
        CVarSetFloat(Key(c, "R").c_str(), r);
    }
    float o = CtrlOpacity(c);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##op", &o, 0.1f, 1.0f, "Opacity %.2f")) {
        CVarSetFloat(Key(c, "O").c_str(), o);
    }
    if (ImGui::Button("Reset this")) {
        CVarClear(Key(c, "X").c_str());
        CVarClear(Key(c, "Y").c_str());
        CVarClear(Key(c, "R").c_str());
        CVarClear(Key(c, "O").c_str());
        CVarSave();
    }
    ImGui::SameLine();
    if (ImGui::Button("Done")) {
        sEdit = false;
        CVarSave();
        SetSohMenuVisible(true);
    }
    ImGui::End();
}

void DrawTouchOverlay() {
    if (!Enabled()) {
        return;
    }
    EnsureWatch();

    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float alpha = Opacity();

    std::vector<Finger> fingers;
    {
        std::lock_guard<std::mutex> lock(sFingerMutex);
        for (const auto& entry : sFingers) {
            ImVec2 start = entry.second;
            auto it = sStarts.find(entry.first);
            if (it != sStarts.end()) {
                start = it->second;
            }
            fingers.push_back({ entry.first, ImVec2(entry.second.x * screen.x, entry.second.y * screen.y),
                                ImVec2(start.x * screen.x, start.y * screen.y) });
        }
    }

    const bool menuOpen = SohMenuVisible();

    // ---- layout editing: the controls are shown and dragged with a finger -------------------------------------
    if (sEdit) {
        sPadButtons = 0;
        sPadStickX = 0;
        sPadStickY = 0;
        sStickActive = false;
        draw->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 90));
        for (int i = 0; i < kControlCount; i++) {
            const Control& c = sControls[i];
            const Geo g = Place(c, screen);
            const float a = std::max(alpha, 0.5f) * CtrlOpacity(c);
            if (c.kind == kStick) {
                DrawStick(draw, g.center, g.center, g.size, a, false, i == sSelected);
            } else {
                DrawControl(draw, c, g, false, a, i == sSelected);
            }
        }

        // Is the dragging finger still down?
        bool found = false;
        for (const Finger& f : fingers) {
            if (f.id == sDragFinger) {
                found = true;
                if (sDragging) {
                    CVarSetFloat(Key(sControls[sSelected], "X").c_str(), std::clamp((f.pos.x + sDragOffset.x) / screen.x, 0.0f, 1.0f));
                    CVarSetFloat(Key(sControls[sSelected], "Y").c_str(), std::clamp((f.pos.y + sDragOffset.y) / screen.y, 0.0f, 1.0f));
                }
            }
        }
        if (!found) {
            if (sDragging) {
                CVarSave();
            }
            sDragging = false;
            sDragFinger = -1;
            if (!io.WantCaptureMouse) {
                for (const Finger& f : fingers) {
                    for (int i = kControlCount - 1; i >= 0 && !sDragging; i--) {
                        const Geo g = Place(sControls[i], screen);
                        if (Hit(sControls[i], g, f.start, 1.2f)) {
                            sSelected = i;
                            sDragging = true;
                            sDragFinger = f.id;
                            sDragOffset = ImVec2(g.center.x - f.pos.x, g.center.y - f.pos.y);
                        }
                    }
                    if (sDragging) {
                        break;
                    }
                }
            }
        }
        DrawEditPanel(screen);
        return;
    }

    // ---- the SoH menu is open: no controls on screen; two fingers dragging scroll the menu ----------------------
    if (menuOpen) {
        sPadButtons = 0;
        sPadStickX = 0;
        sPadStickY = 0;
        sStickActive = false;
        sMenuWasDown = true; // a finger still resting on the MENU button must not toggle again
        if (fingers.size() >= 2) {
            const float avgY = (fingers[0].pos.y + fingers[1].pos.y) * 0.5f;
            if (sScrollPrevY >= 0.0f) {
                io.AddMouseWheelEvent(0.0f, (avgY - sScrollPrevY) / (5.0f * ImGui::GetFontSize()));
            }
            sScrollPrevY = avgY;
        } else {
            sScrollPrevY = -1.0f;
        }
        return;
    }
    sScrollPrevY = -1.0f;

    // ---- normal play --------------------------------------------------------------------------------------------
    float stickX = 0.0f, stickY = 0.0f;
    uint16_t bits = 0;

    // The stick.
    {
        const Control& sc = sControls[kStickIndex];
        const Geo g = Place(sc, screen);
        if (sStickActive) {
            bool still = false;
            for (const Finger& f : fingers) {
                still = still || f.id == sStickFinger;
            }
            sStickActive = still;
        }
        if (!sStickActive) {
            for (const Finger& f : fingers) {
                // A finger claims the stick if it first touched on the left side of the screen (floating) or
                // on the stick itself (fixed), and not on another control.
                bool onOther = false;
                for (int i = 1; i < kControlCount; i++) {
                    if (Hit(sControls[i], Place(sControls[i], screen), f.start, 1.15f)) {
                        onOther = true;
                    }
                }
                const bool inZone = FloatingStick() ? (f.start.x < screen.x * 0.45f && f.start.y > screen.y * 0.18f)
                                                    : Dist(f.start, g.center) <= g.size * 1.5f;
                if (inZone && !onOther) {
                    sStickActive = true;
                    sStickFinger = f.id;
                    if (FloatingStick()) {
                        sStickBase = ImVec2(std::clamp(f.start.x, g.size, screen.x - g.size),
                                            std::clamp(f.start.y, g.size, screen.y - g.size));
                    } else {
                        sStickBase = g.center;
                    }
                    break;
                }
            }
        }
        ImVec2 knob = g.center;
        if (sStickActive) {
            for (const Finger& f : fingers) {
                if (f.id != sStickFinger) {
                    continue;
                }
                float dx = f.pos.x - sStickBase.x, dy = f.pos.y - sStickBase.y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d > g.size && FloatingStick()) {
                    // Dragged past the edge: the base follows the thumb, so the stick never runs out of reach.
                    const float over = d - g.size;
                    sStickBase.x += dx / d * over;
                    sStickBase.y += dy / d * over;
                    dx = f.pos.x - sStickBase.x;
                    dy = f.pos.y - sStickBase.y;
                    d = g.size;
                }
                stickX = std::clamp(dx / g.size, -1.0f, 1.0f);
                stickY = std::clamp(dy / g.size, -1.0f, 1.0f);
                knob = ImVec2(sStickBase.x + std::clamp(dx, -g.size, g.size), sStickBase.y + std::clamp(dy, -g.size, g.size));
            }
        }
        DrawStick(draw, sStickActive ? sStickBase : g.center, sStickActive ? knob : g.center, g.size,
                  alpha * CtrlOpacity(sc), sStickActive, false);
    }

    // Buttons.
    for (int i = 1; i < kControlCount; i++) {
        const Control& c = sControls[i];
        if (i == kMenuIndex && !ShowMenuButton()) {
            continue;
        }
        const Geo g = Place(c, screen);
        bool pressed = false;
        for (const Finger& f : fingers) {
            if (sStickActive && f.id == sStickFinger) {
                continue;
            }
            if (Hit(c, g, f.pos, 1.15f)) {
                pressed = true;
            }
        }
        if (i == kMenuIndex) {
            if (pressed && !sMenuWasDown) {
                SetSohMenuVisible(true);
            }
            sMenuWasDown = pressed;
        } else if (pressed) {
            bits |= c.bit;
        }
        DrawControl(draw, c, g, pressed, alpha * CtrlOpacity(c), false);
    }

    // The stick: a small dead zone, then the full N64 range (about +-85).
    const float mag = std::sqrt(stickX * stickX + stickY * stickY);
    int sx = 0, sy = 0;
    if (mag > 0.10f) {
        const float scaled = std::min(1.0f, (mag - 0.10f) / 0.85f) / mag * 85.0f;
        sx = static_cast<int>(std::lround(stickX * scaled));
        sy = static_cast<int>(std::lround(-stickY * scaled)); // screen y grows downwards
    }
    sPadButtons = bits;
    sPadStickX = sx;
    sPadStickY = sy;
}

// The settings, drawn inside the SoH menu (Settings > Touch Controls).
void DrawTouchTab() {
    bool enabled = Enabled();
    if (ImGui::Checkbox("Show touch controls", &enabled)) {
        CVarSetInteger("gAndroid.Touch.Enabled", enabled ? 1 : 0);
        CVarSave();
    }
    bool floating = FloatingStick();
    if (ImGui::Checkbox("Floating stick (appears where you touch, left side)", &floating)) {
        CVarSetInteger("gAndroid.Touch.FloatingStick", floating ? 1 : 0);
        CVarSave();
    }
    bool showMenu = ShowMenuButton();
    if (ImGui::Checkbox("Show the MENU button (the back button always opens this menu)", &showMenu)) {
        CVarSetInteger("gAndroid.Touch.ShowMenuButton", showMenu ? 1 : 0);
        CVarSave();
    }
    float opacity = Opacity();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    if (ImGui::SliderFloat("Opacity (all)", &opacity, 0.1f, 1.0f, "%.2f")) {
        CVarSetFloat("gAndroid.Touch.Opacity", opacity);
        CVarSave();
    }
    float scale = Scale();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    if (ImGui::SliderFloat("Size (all)", &scale, 0.5f, 2.0f, "%.2fx")) {
        CVarSetFloat("gAndroid.Touch.Scale", scale);
        CVarSave();
    }
    float ui = CVarGetFloat("gAndroid.UiScale", 0.0f);
    if (ui <= 0.0f) {
        ui = ImGui::GetIO().FontGlobalScale;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    if (ImGui::SliderFloat("Menu text size", &ui, 0.8f, 3.0f, "%.2fx")) {
        CVarSetFloat("gAndroid.UiScale", ui);
        ImGui::GetIO().FontGlobalScale = ui;
        CVarSave();
    }

    ImGui::Spacing();
    if (ImGui::Button("Edit layout")) {
        // Closes this menu and shows the controls so each one (MENU included) can be dragged,
        // resized and given its own opacity.
        sEdit = true;
        SetSohMenuVisible(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset layout")) {
        for (const Control& c : sControls) {
            CVarClear(Key(c, "X").c_str());
            CVarClear(Key(c, "Y").c_str());
            CVarClear(Key(c, "R").c_str());
            CVarClear(Key(c, "O").c_str());
        }
        CVarSetFloat("gAndroid.Touch.Scale", 1.0f);
        CVarSave();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Stick = move, A / B, C buttons, Z, L, R, Start.");
    ImGui::TextDisabled("Change what each button does in Settings > Controls.");
}

} // namespace SohAndroid

// Called by padmgr.c right after the game read the controllers: adds what the fingers are doing.
extern "C" void SohAndroid_InjectTouchPad(void* data) {
    OSContPad* pad = static_cast<OSContPad*>(data);
    const uint16_t bits = sPadButtons.load();
    const int sx = sPadStickX.load();
    const int sy = sPadStickY.load();
    if (pad == nullptr || (bits == 0 && sx == 0 && sy == 0)) {
        return;
    }
    pad->button |= bits;
    if (sx != 0 || sy != 0) {
        pad->stick_x = static_cast<int8_t>(sx);
        pad->stick_y = static_cast<int8_t>(sy);
    }
}

#ifdef __ANDROID__
#include <jni.h>
// Called by SohActivity when the back button is pressed (UI thread): only sets a flag, the menu is toggled on the
// render thread by HandleBackButton().
extern "C" JNIEXPORT void JNICALL Java_com_shipofharkinian_SohActivity_nativeBackPressed(JNIEnv*, jclass) {
    sBackPressed = true;
}
#endif
