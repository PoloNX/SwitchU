#include <nxui/core/Input.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <algorithm>
#include <cmath>

namespace nxui {

namespace {

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
    if (style & HidNpadStyleTag_NpadFullKey)  return hidGetNpadStatesFullKey(id, &state, 1);
    if (style & HidNpadStyleTag_NpadHandheld) return hidGetNpadStatesHandheld(id, &state, 1);
    if (style & HidNpadStyleTag_NpadJoyDual)  return hidGetNpadStatesJoyDual(id, &state, 1);
    if (style & HidNpadStyleTag_NpadJoyLeft)  return hidGetNpadStatesJoyLeft(id, &state, 1);
    if (style & HidNpadStyleTag_NpadJoyRight) return hidGetNpadStatesJoyRight(id, &state, 1);
    return 0;
}

constexpr HidNpadIdType kNpadIds[] = {
    HidNpadIdType_Handheld,
    HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
    HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8,
};

} // namespace

void Input::initialize() {
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    hidInitializeTouchScreen();
}

void Input::shutdown() {
}

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
                                       | HidNpadStyleTag_NpadJoyDual));
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
    m_kDown = held & ~m_kHeld;
    m_kUp   = m_kHeld & ~held;
    m_kHeld = held;

    // Analog sticks
    m_lx = ls.x / 32767.f;
    m_ly = ls.y / 32767.f;
    m_rx = rs.x / 32767.f;
    m_ry = rs.y / 32767.f;

    HidTouchScreenState tState = {};
    bool hardwareTouching = hidGetTouchScreenStates(&tState, 1) && tState.count > 0;
    float hardwareTouchX = hardwareTouching ? tState.touches[0].x : m_touchX;
    float hardwareTouchY = hardwareTouching ? tState.touches[0].y : m_touchY;

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
