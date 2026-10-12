// Touch controls (touch_controls.hpp).
//
// Layout follows GalaxyPad's SunPad-derived design: movement lower left,
// an asymmetric A/B/X/Y cluster lower right with Z beside the stick, and small
// pills for +/- and 1/2. X is Spin (a Wii Remote flick) and Y is Nunchuk C.
// Each finger keeps the control it first touched until it lifts. A finger that
// starts on no control drives the Star Pointer: the pointer follows it exactly
// and stays where it was released, so A or B can then be pressed.

#include "touch_controls.hpp"

#include <SDL3/SDL_events.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <TargetConditionals.h>

#include "petari/input.hpp"

namespace PetariNative::App::TouchControls {
namespace {

using Input::PadButton;

enum class Kind { Button, Key, Stick, Pointer };

struct Control {
    const char* label;
    Kind kind;
    PadButton pad;
    Input::KeyCode key;
    // Centre relative to an anchor, in units; radius in units.
    float x, y, r;
    bool rightAnchor;  // anchored to the lower-right corner, else lower-left
    bool pill;
    // Phone layout (screens wider than 1.9:1): the right-hand controls stand in a
    // column inside the pillarbox bar, clear of the game's HUD counters.
    float px, py, pr;
    bool held = false;
};

std::array<Control, 10> gControls = {{
    {"A", Kind::Button, PadButton::South, 0, -1.45f, -1.25f, 0.78f, true, false, -1.15f, -1.20f, 0.74f},
    {"B", Kind::Button, PadButton::East, 0, -3.05f, -0.80f, 0.56f, true, false, -1.15f, -4.40f, 0.52f},
    {"X", Kind::Button, PadButton::West, 0, -0.95f, -3.00f, 0.60f, true, false, -1.15f, -2.85f, 0.58f},
    {"Y", Kind::Button, PadButton::LeftShoulder, 0, -2.55f, -2.45f, 0.48f, true, false, -1.15f, -5.80f, 0.46f},
    {"Z", Kind::Button, PadButton::LeftTrigger, 0, 3.75f, -3.35f, 0.50f, false, false, 3.75f, -3.35f, 0.50f},
    {"+", Kind::Button, PadButton::Start, 0, -1.05f, -4.55f, 0.36f, true, true, -0.70f, -7.25f, 0.32f},
    {"-", Kind::Button, PadButton::Back, 0, -2.15f, -4.55f, 0.36f, true, true, -1.65f, -7.25f, 0.32f},
    {"1", Kind::Key, PadButton::Count, Input::Key::Num1, 1.10f, -5.20f, 0.36f, false, true, 1.10f, -5.20f, 0.36f},
    {"2", Kind::Key, PadButton::Count, Input::Key::Num2, 2.20f, -5.20f, 0.36f, false, true, 2.20f, -5.20f, 0.36f},
    {"Home", Kind::Button, PadButton::Guide, 0, 3.75f, -5.20f, 0.36f, false, true, 3.75f, -5.20f, 0.36f},
}};

struct Finger {
    SDL_FingerID id = 0;
    bool active = false;
    Kind kind = Kind::Pointer;
    int control = -1;
    float originX = 0.0f, originY = 0.0f;  // stick: where the finger landed
    float x = 0.0f, y = 0.0f;
};
std::array<Finger, 10> gFingers;

float gWidth = 0.0f, gHeight = 0.0f;
float gStickX = 0.0f, gStickY = 0.0f;  // -1..1, y down
bool gStickActive = false;
float gStickOriginX = 0.0f, gStickOriginY = 0.0f, gStickKnobX = 0.0f, gStickKnobY = 0.0f;

float unit() { return std::clamp(std::min(gWidth, gHeight) * 0.085f, 26.0f, 58.0f); }
bool phone() { return gHeight > 0.0f && gWidth / gHeight > 1.9f; }
float radius(const Control& c) { return phone() ? c.pr : c.r; }
float margin() { return unit() * 0.55f; }
float stickRadius() { return unit() * 1.35f; }
void stickHome(float* x, float* y) {
    *x = margin() + stickRadius() * 1.25f;
    *y = gHeight - margin() - stickRadius() * 1.25f;
}

void centre(const Control& c, float* x, float* y) {
    const float u = unit();
    const float ax = c.rightAnchor ? gWidth - margin() : margin();
    const float ay = gHeight - margin();
    *x = ax + (phone() ? c.px : c.x) * u;
    *y = ay + (phone() ? c.py : c.y) * u;
}

int hitControl(float x, float y) {
    const float u = unit();
    int best = -1;
    float bestDistance = 0.0f;
    for (int i = 0; i < static_cast<int>(gControls.size()); ++i) {
        float cx, cy;
        centre(gControls[i], &cx, &cy);
        const float d = std::hypot(x - cx, y - cy);
        const float reach = radius(gControls[i]) * u * 1.25f;
        if (d <= reach && (best < 0 || d < bestDistance)) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

// A circle around the stick's resting place: the rest of the screen stays free
// for the Star Pointer, including menu items in the lower left.
bool inStickZone(float x, float y) {
    float hx, hy;
    stickHome(&hx, &hy);
    return std::hypot(x - hx, y - hy) <= stickRadius() * 1.7f;
}

void setControl(int index, bool down) {
    Control& c = gControls[index];
    if (c.held == down) return;
    c.held = down;
    if (c.kind == Kind::Key) {
        Input::keyEvent(c.key, down, false);
    } else {
        Input::padButtonEvent(c.pad, down);
    }
}

void refreshHeld() {
    std::array<bool, gControls.size()> wanted{};
    for (const Finger& f : gFingers) {
        if (f.active && f.kind == Kind::Button && f.control >= 0) wanted[f.control] = true;
    }
    for (int i = 0; i < static_cast<int>(gControls.size()); ++i) setControl(i, wanted[i]);
}

void updateStick(const Finger& f) {
    const float r = stickRadius();
    float dx = f.x - f.originX, dy = f.y - f.originY;
    const float length = std::hypot(dx, dy);
    if (length > r) {
        dx *= r / length;
        dy *= r / length;
    }
    gStickX = dx / r;
    gStickY = dy / r;
    gStickActive = true;
    gStickOriginX = f.originX;
    gStickOriginY = f.originY;
    gStickKnobX = f.originX + dx;
    gStickKnobY = f.originY + dy;
    Input::padAxisEvent(Input::PadAxis::LeftX, gStickX);
    Input::padAxisEvent(Input::PadAxis::LeftY, gStickY);
}

void releaseStick() {
    gStickActive = false;
    gStickX = gStickY = 0.0f;
    Input::padAxisEvent(Input::PadAxis::LeftX, 0.0f);
    Input::padAxisEvent(Input::PadAxis::LeftY, 0.0f);
}

Finger* finger(SDL_FingerID id) {
    for (Finger& f : gFingers) {
        if (f.active && f.id == id) return &f;
    }
    return nullptr;
}

}  // namespace

bool enabled() {
    static const bool on = [] {
        const char* value = std::getenv("PETARI_TOUCH_CONTROLS");
        if (value && *value) return std::strcmp(value, "0") != 0;
#if TARGET_OS_IPHONE
        return true;
#else
        return false;
#endif
    }();
    return on;
}

void setWindow(float width, float height) {
    gWidth = width;
    gHeight = height;
}

bool handle(const SDL_Event& event) {
    if (!enabled() || gWidth <= 0.0f || gHeight <= 0.0f) return false;
    if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
        event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED) {
        return false;
    }
    const float x = event.tfinger.x * gWidth;
    const float y = event.tfinger.y * gHeight;
    if (event.type == SDL_EVENT_FINGER_DOWN) {
        Finger* slot = nullptr;
        for (Finger& f : gFingers) {
            if (!f.active) {
                slot = &f;
                break;
            }
        }
        if (slot == nullptr) return true;
        *slot = Finger{};
        slot->id = event.tfinger.fingerID;
        slot->active = true;
        slot->x = slot->originX = x;
        slot->y = slot->originY = y;
        const int control = hitControl(x, y);
        const bool stickTaken = std::any_of(gFingers.begin(), gFingers.end(),
                                            [&](const Finger& f) { return f.active && &f != slot && f.kind == Kind::Stick; });
        if (control >= 0) {
            slot->kind = Kind::Button;
            slot->control = control;
            refreshHeld();
        } else if (!stickTaken && inStickZone(x, y)) {
            slot->kind = Kind::Stick;
            updateStick(*slot);
        } else {
            slot->kind = Kind::Pointer;
            Input::focusChanged(true);
            Input::mouseMoved(x, y);
        }
        return true;
    }
    Finger* f = finger(event.tfinger.fingerID);
    if (f == nullptr) return true;
    f->x = x;
    f->y = y;
    if (event.type == SDL_EVENT_FINGER_MOTION) {
        if (f->kind == Kind::Stick) {
            updateStick(*f);
        } else if (f->kind == Kind::Pointer) {
            Input::mouseMoved(x, y);
        } else if (f->kind == Kind::Button) {
            // Sliding between face buttons switches the held button, as a thumb
            // rolling from B to A would; sliding off keeps the first one.
            const int control = hitControl(x, y);
            if (control >= 0 && control != f->control && gControls[control].kind == gControls[f->control].kind &&
                !gControls[control].pill && !gControls[f->control].pill) {
                f->control = control;
                refreshHeld();
            }
        }
        return true;
    }
    // Up or cancelled.
    const Kind kind = f->kind;
    f->active = false;
    if (kind == Kind::Stick) releaseStick();
    if (kind == Kind::Button) refreshHeld();
    if (kind == Kind::Pointer && event.type == SDL_EVENT_FINGER_CANCELED) Input::mouseLeft();
    return true;
}

void releaseAll() {
    for (Finger& f : gFingers) f.active = false;
    refreshHeld();
    releaseStick();
}

bool smokeViaTouch() {
    static const bool on = [] {
        const char* value = std::getenv("PETARI_SMOKE_VIA_TOUCH");
        return value && std::strcmp(value, "1") == 0;
    }();
    return on && enabled();
}

namespace {
void sendFinger(Uint32 type, SDL_FingerID id, float x, float y) {
    SDL_Event event{};
    event.type = type;
    event.tfinger.fingerID = id;
    event.tfinger.x = x / gWidth;
    event.tfinger.y = y / gHeight;
    handle(event);
}
int controlIndex(const char* label) {
    for (int i = 0; i < static_cast<int>(gControls.size()); ++i) {
        if (std::strcmp(gControls[i].label, label) == 0) return i;
    }
    return -1;
}
bool gDirections[4] = {};  // up, down, left, right
bool gStickFingerDown = false;
constexpr SDL_FingerID kStickFinger = 9000, kPointerFinger = 9100, kButtonFinger = 9200;
}  // namespace

void synthetic(Synthetic which, bool down) {
    if (gWidth <= 0.0f) return;
    const char* label = nullptr;
    switch (which) {
    case Synthetic::A: label = "A"; break;
    case Synthetic::B: label = "B"; break;
    case Synthetic::Spin: label = "X"; break;
    case Synthetic::Z: label = "Z"; break;
    case Synthetic::Plus: label = "+"; break;
    case Synthetic::Minus: label = "-"; break;
    default: break;
    }
    if (label != nullptr) {
        const int index = controlIndex(label);
        float x, y;
        centre(gControls[index], &x, &y);
        sendFinger(down ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP, kButtonFinger + index, x, y);
        return;
    }
    gDirections[static_cast<int>(which) - static_cast<int>(Synthetic::StickUp)] = down;
    const float dx = (gDirections[3] ? 1.0f : 0.0f) - (gDirections[2] ? 1.0f : 0.0f);
    const float dy = (gDirections[1] ? 1.0f : 0.0f) - (gDirections[0] ? 1.0f : 0.0f);
    float hx, hy;
    stickHome(&hx, &hy);
    const bool any = dx != 0.0f || dy != 0.0f;
    if (any && !gStickFingerDown) {
        sendFinger(SDL_EVENT_FINGER_DOWN, kStickFinger, hx, hy);
        gStickFingerDown = true;
    }
    if (any) {
        const float length = std::hypot(dx, dy);
        sendFinger(SDL_EVENT_FINGER_MOTION, kStickFinger, hx + dx / length * stickRadius() * 1.2f,
                   hy + dy / length * stickRadius() * 1.2f);
    } else if (gStickFingerDown) {
        sendFinger(SDL_EVENT_FINGER_UP, kStickFinger, hx, hy);
        gStickFingerDown = false;
    }
}

void syntheticPointer(float x, float y) {
    if (gWidth <= 0.0f) return;
    sendFinger(SDL_EVENT_FINGER_DOWN, kPointerFinger, x, y);
    sendFinger(SDL_EVENT_FINGER_UP, kPointerFinger, x, y);
}

void draw() {
    if (!enabled() || gWidth <= 0.0f) return;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float s = display.x > 0.0f ? display.x / gWidth : 1.0f;  // points to ImGui units
    const float u = unit();
    const ImU32 fill = IM_COL32(255, 255, 255, 40), fillHeld = IM_COL32(255, 255, 255, 120);
    const ImU32 edge = IM_COL32(255, 255, 255, 150), text = IM_COL32(255, 255, 255, 220);
    ImFont* font = ImGui::GetFont();
    for (const Control& c : gControls) {
        float cx, cy;
        centre(c, &cx, &cy);
        const float r = radius(c) * u;
        const ImVec2 p(cx * s, cy * s);
        if (c.pill) {
            const float half = std::max(r * 1.3f, font->CalcTextSizeA(r * 1.1f * s, 1e9f, 0.0f, c.label).x / s * 0.5f + r * 0.6f);
            const ImVec2 a((cx - half) * s, (cy - r * 0.75f) * s), b((cx + half) * s, (cy + r * 0.75f) * s);
            list->AddRectFilled(a, b, c.held ? fillHeld : fill, r * 0.75f * s);
            list->AddRect(a, b, edge, r * 0.75f * s, 0, 1.5f * s);
        } else {
            list->AddCircleFilled(p, r * s, c.held ? fillHeld : fill, 40);
            list->AddCircle(p, r * s, edge, 40, 2.0f * s);
        }
        const float size = (c.pill ? r * 1.1f : r * 0.95f) * s;
        const ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0.0f, c.label);
        list->AddText(font, size, ImVec2(p.x - extent.x * 0.5f, p.y - extent.y * 0.5f), text, c.label);
    }
    float hx, hy;
    stickHome(&hx, &hy);
    const float ox = gStickActive ? gStickOriginX : hx, oy = gStickActive ? gStickOriginY : hy;
    const float kx = gStickActive ? gStickKnobX : hx, ky = gStickActive ? gStickKnobY : hy;
    list->AddCircleFilled(ImVec2(ox * s, oy * s), stickRadius() * s, IM_COL32(255, 255, 255, 25), 48);
    list->AddCircle(ImVec2(ox * s, oy * s), stickRadius() * s, edge, 48, 2.0f * s);
    list->AddCircleFilled(ImVec2(kx * s, ky * s), stickRadius() * 0.45f * s, gStickActive ? fillHeld : fill, 32);
}

}  // namespace PetariNative::App::TouchControls

