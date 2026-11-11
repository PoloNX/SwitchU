#include <nxui/core/Input.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <algorithm>
#include <cmath>

namespace nxui {

namespace {

constexpr float kFbWidth = 1280.f;
constexpr float kFbHeight = 720.f;

void rotateVec2CwQuarters(float& x, float& y, int quarters) {
    quarters = ((quarters % 4) + 4) % 4;
    for (int i = 0; i < quarters; ++i) {
        const float nx = y;
        const float ny = -x;
        x = nx;
        y = ny;
    }
}

// Inverse of CW-N*90 presentation: panel/glass coords → framebuffer space.
// Normalized unit-square rotation keeps corners mapped for the non-square FB.
void mapTouchToFramebuffer(float& x, float& y, DisplayOrientation orientation) {
    if (orientation == DisplayOrientation::Landscape0)
        return;
    const float u = std::clamp(x / kFbWidth, 0.f, 1.f);
    const float v = std::clamp(y / kFbHeight, 0.f, 1.f);
    float iu = u;
    float iv = v;
    switch (orientation) {
        case DisplayOrientation::Rot90:  // inverse CW90 → (1-v, u)
            iu = 1.f - v;
            iv = u;
            break;
        case DisplayOrientation::Rot180:
            iu = 1.f - u;
            iv = 1.f - v;
            break;
        case DisplayOrientation::Rot270: // inverse CW270 → (v, 1-u)
            iu = v;
            iv = 1.f - u;
            break;
        case DisplayOrientation::Landscape0:
        default:
            break;
    }
    x = iu * kFbWidth;
    y = iv * kFbHeight;
}

uint64_t rotateDirectionBits(uint64_t buttons, DisplayOrientation orientation) {
    if (orientation == DisplayOrientation::Landscape0)
        return buttons;

    const int quarters = static_cast<int>(orientation);
    auto rotPad = [quarters](uint64_t left, uint64_t up, uint64_t right, uint64_t down,
                             uint64_t bits) {
        const bool l = (bits & left) != 0;
        const bool u = (bits & up) != 0;
        const bool r = (bits & right) != 0;
        const bool d = (bits & down) != 0;
        // Rotate the pressed direction CW by `quarters` so focus matches the
        // transformed UI (same sense as stick vector rotation).
        bool ol = l, ou = u, orr = r, od = d;
        for (int i = 0; i < quarters; ++i) {
            const bool nl = od;
            const bool nu = ol;
            const bool nr = ou;
            const bool nd = orr;
            ol = nl; ou = nu; orr = nr; od = nd;
        }
        uint64_t out = bits & ~(left | up | right | down);
        if (ol) out |= left;
        if (ou) out |= up;
        if (orr) out |= right;
        if (od) out |= down;
        return out;
    };

    buttons = rotPad(HidNpadButton_Left, HidNpadButton_Up,
                     HidNpadButton_Right, HidNpadButton_Down, buttons);
    buttons = rotPad(HidNpadButton_StickLLeft, HidNpadButton_StickLUp,
                     HidNpadButton_StickLRight, HidNpadButton_StickLDown, buttons);
    buttons = rotPad(HidNpadButton_StickRLeft, HidNpadButton_StickRUp,
                     HidNpadButton_StickRRight, HidNpadButton_StickRDown, buttons);
    return buttons;
}

// A single Joy-Con arrives as an upright controller (libnx's pad merges it raw,
// whatever the hold type). Held sideways - the only way HOME is used with one -
// its four buttons act as A/B/X/Y by where they sit, SL/SR as L/R, and its
// stick is turned a quarter: counter-clockwise for the left Joy-Con, clockwise
// for the right one.
struct ButtonMap { uint64_t from, to; };

constexpr ButtonMap kLeftJoyConSideways[] = {
    {HidNpadButton_Down,  HidNpadButton_A},
    {HidNpadButton_Left,  HidNpadButton_B},
    {HidNpadButton_Right, HidNpadButton_X},
    {HidNpadButton_Up,    HidNpadButton_Y},
    {HidNpadButton_LeftSL, HidNpadButton_L},
    {HidNpadButton_LeftSR, HidNpadButton_R},
};

constexpr ButtonMap kRightJoyConSideways[] = {
    {HidNpadButton_X, HidNpadButton_A},
    {HidNpadButton_A, HidNpadButton_B},
    {HidNpadButton_Y, HidNpadButton_X},
    {HidNpadButton_B, HidNpadButton_Y},
    {HidNpadButton_RightSL, HidNpadButton_L},
    {HidNpadButton_RightSR, HidNpadButton_R},
};

constexpr uint64_t kStickDirectionBits =
    HidNpadButton_StickLLeft | HidNpadButton_StickLUp
    | HidNpadButton_StickLRight | HidNpadButton_StickLDown
    | HidNpadButton_StickRLeft | HidNpadButton_StickRUp
    | HidNpadButton_StickRRight | HidNpadButton_StickRDown;

// Half deflection, like the pseudo-buttons HID derives from an upright stick.
constexpr int32_t kStickDirectionThreshold = 16384;

uint64_t leftStickDirections(const HidAnalogStickState& stick) {
    uint64_t bits = 0;
    if (stick.x <= -kStickDirectionThreshold) bits |= HidNpadButton_StickLLeft;
    if (stick.x >=  kStickDirectionThreshold) bits |= HidNpadButton_StickLRight;
    if (stick.y >=  kStickDirectionThreshold) bits |= HidNpadButton_StickLUp;
    if (stick.y <= -kStickDirectionThreshold) bits |= HidNpadButton_StickLDown;
    return bits;
}

// Buttons of a sideways Joy-Con; its stick becomes the left stick.
uint64_t mapSidewaysJoyCon(const HidNpadCommonState& state, bool left,
                           HidAnalogStickState& stick) {
    uint64_t buttons = state.buttons & ~kStickDirectionBits;
    for (const auto& map : left ? kLeftJoyConSideways : kRightJoyConSideways)
        buttons &= ~map.from;
    for (const auto& map : left ? kLeftJoyConSideways : kRightJoyConSideways)
        if (state.buttons & map.from)
            buttons |= map.to;

    const HidAnalogStickState& raw = left ? state.analog_stick_l : state.analog_stick_r;
    stick.x = left ? -raw.y : raw.y;
    stick.y = left ? raw.x : -raw.x;
    return buttons | leftStickDirections(stick);
}

size_t readNpadState(HidNpadIdType id, uint32_t style, HidNpadCommonState& state) {
    // GameCube uses a sibling state (same button/stick lifo as FullKey plus
    // analog triggers). Prefer it when the pad is in Gc style so we do not
    // miss adapters that only advertise NpadGc.
    if (style & HidNpadStyleTag_NpadGc) {
        HidNpadGcState gc{};
        const size_t n = hidGetNpadStatesGc(id, &gc, 1);
        if (n == 0)
            return 0;
        state.sampling_number = gc.sampling_number;
        state.buttons = gc.buttons;
        state.analog_stick_l = gc.analog_stick_l;
        state.analog_stick_r = gc.analog_stick_r;
        state.attributes = gc.attributes;
        state.reserved = 0;
        return n;
    }
    if (style & HidNpadStyleTag_NpadFullKey)  return hidGetNpadStatesFullKey(id, &state, 1);
    if (style & HidNpadStyleTag_NpadHandheld) return hidGetNpadStatesHandheld(id, &state, 1);
    if (style & HidNpadStyleTag_NpadJoyDual)  return hidGetNpadStatesJoyDual(id, &state, 1);
    if (style & HidNpadStyleTag_NpadJoyLeft)  return hidGetNpadStatesJoyLeft(id, &state, 1);
    if (style & HidNpadStyleTag_NpadJoyRight) return hidGetNpadStatesJoyRight(id, &state, 1);
    if (style & HidNpadStyleTag_NpadSystemExt) return hidGetNpadStatesSystemExt(id, &state, 1);
    return 0;
}

// Standard Joy/Pro set plus GameCube adapters and generic external pads so
// Player 2+ / USB GC can drive HOME (issue #99). NpadStandard alone drops Gc.
constexpr uint32_t kSupportedNpadStyles =
    HidNpadStyleSet_NpadStandard
    | HidNpadStyleTag_NpadGc
    | HidNpadStyleTag_NpadSystemExt;

constexpr HidNpadIdType kNpadIds[] = {
    HidNpadIdType_Handheld,
    HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
    HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8,
};

} // namespace

void Input::initialize() {
    padConfigureInput(8, kSupportedNpadStyles);
    hidInitializeTouchScreen();
}

void Input::shutdown() {
}

void Input::setDisplayOrientation(DisplayOrientation orientation) {
    m_orientation = orientation;
}

void Input::setLogicalLayoutSize(float width, float height) {
    m_logicalW = width > 0.f ? width : kFbWidth;
    m_logicalH = height > 0.f ? height : kFbHeight;
}

namespace {

bool isPortraitOrientation(DisplayOrientation orientation) {
    return orientation == DisplayOrientation::Rot90
        || orientation == DisplayOrientation::Rot270;
}

// FB/panel (nwindow identity) → logical portrait. Matches Renderer orthoPortrait.
void mapFramebufferToLogical(float& x, float& y, DisplayOrientation orientation) {
    const float fx = x;
    const float fy = y;
    switch (orientation) {
        case DisplayOrientation::Rot90:  // inverse of (ly, H-lx)
            x = kFbHeight - fy;
            y = fx;
            break;
        case DisplayOrientation::Rot270: // inverse of (W-ly, lx)
            x = fy;
            y = kFbWidth - fx;
            break;
        default:
            break;
    }
}

} // namespace

void Input::update() {
    // Every controller is read on its own (padUpdate merged them raw, which
    // left a sideways Joy-Con with turned-around buttons and stick).
    uint64_t held = 0;
    HidAnalogStickState ls{}, rs{};
    auto takeMoving = [](HidAnalogStickState& out, const HidAnalogStickState& stick) {
        if (stick.x != 0 || stick.y != 0)
            out = stick;
    };
    for (const HidNpadIdType id : kNpadIds) {
        const uint32_t style = hidGetNpadStyleSet(id);
        if (style == 0)
            continue;
        HidNpadCommonState state{};
        if (readNpadState(id, style, state) == 0
            || !(state.attributes & HidNpadAttribute_IsConnected))
            continue;

        const bool single = !(style & (HidNpadStyleTag_NpadFullKey
                                       | HidNpadStyleTag_NpadHandheld
                                       | HidNpadStyleTag_NpadJoyDual
                                       | HidNpadStyleTag_NpadGc
                                       | HidNpadStyleTag_NpadSystemExt));
        const bool leftJoyCon = single && (style & HidNpadStyleTag_NpadJoyLeft);
        const bool rightJoyCon = single && (style & HidNpadStyleTag_NpadJoyRight);
        if (leftJoyCon || rightJoyCon) {
            const uint32_t bit = id == HidNpadIdType_Handheld
                ? (1u << 9) : (1u << static_cast<uint32_t>(id));
            if (!(m_loggedSingleJoyCons & bit)) {
                m_loggedSingleJoyCons |= bit;
                GpuDevice::logGpu("[input] single %s Joy-Con on npad %u: sideways mapping",
                                  leftJoyCon ? "left" : "right", static_cast<unsigned>(id));
            }
            HidAnalogStickState stick{};
            held |= mapSidewaysJoyCon(state, leftJoyCon, stick);
            takeMoving(ls, stick);
            continue;
        }
        held |= state.buttons;
        takeMoving(ls, state.analog_stick_l);
        takeMoving(rs, state.analog_stick_r);
    }
    // Landscape/180: remap for nwindow. Portrait: logical upright (projection
    // owns the rotate; nwindow is identity) — skip CW remaps.
    if (!isPortraitOrientation(m_orientation))
        held = rotateDirectionBits(held, m_orientation);
    m_kDown = held & ~m_kHeld;
    m_kUp   = m_kHeld & ~held;
    m_kHeld = held;

    // Analog sticks
    m_lx = ls.x / 32767.f;
    m_ly = ls.y / 32767.f;
    m_rx = rs.x / 32767.f;
    m_ry = rs.y / 32767.f;
    if (!isPortraitOrientation(m_orientation)) {
        rotateVec2CwQuarters(m_lx, m_ly, static_cast<int>(m_orientation));
        rotateVec2CwQuarters(m_rx, m_ry, static_cast<int>(m_orientation));
    }

    HidTouchScreenState tState = {};
    bool hardwareTouching = hidGetTouchScreenStates(&tState, 1) && tState.count > 0;
    float hardwareTouchX = hardwareTouching ? static_cast<float>(tState.touches[0].x) : m_touchX;
    float hardwareTouchY = hardwareTouching ? static_cast<float>(tState.touches[0].y) : m_touchY;
    if (hardwareTouching) {
        if (isPortraitOrientation(m_orientation)) {
            // nwindow identity → panel == FB, then into logical 720x1280.
            mapFramebufferToLogical(hardwareTouchX, hardwareTouchY, m_orientation);
        } else {
            mapTouchToFramebuffer(hardwareTouchX, hardwareTouchY, m_orientation);
        }
    }

    m_wasTouching = m_touching;
    if (hardwareTouching) {
        m_touchX = hardwareTouchX;
        m_touchY = hardwareTouchY;
    }

    m_touching = hardwareTouching;

    m_touchDown = (m_touching && !m_wasTouching);
    m_touchUp   = (!m_touching && m_wasTouching);

    if (m_touchDown) {
        m_touchStartX = m_touchX;
        m_touchStartY = m_touchY;
        m_touchStartTick = armGetSystemTick();
    }
}

float Input::touchDuration() const {
    uint64_t now = armGetSystemTick();
    return static_cast<float>(now - m_touchStartTick) / static_cast<float>(armGetSystemTickFreq());
}

bool Input::isDown(Button b) const { return m_kDown & static_cast<uint64_t>(b); }
bool Input::isUp(Button b)   const { return m_kUp   & static_cast<uint64_t>(b); }
bool Input::isHeld(Button b) const { return m_kHeld & static_cast<uint64_t>(b); }

bool Input::hasUserActivity() const {
    constexpr float kActivityStickThreshold = 0.08f;
    return m_kDown != 0 || m_kUp != 0 || m_touching || m_touchDown || m_touchUp
        || std::abs(m_lx) >= kActivityStickThreshold
        || std::abs(m_ly) >= kActivityStickThreshold
        || std::abs(m_rx) >= kActivityStickThreshold
        || std::abs(m_ry) >= kActivityStickThreshold;
}

} // namespace nxui
