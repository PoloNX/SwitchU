#include <nxui/core/Input.hpp>
#include <algorithm>
#include <cmath>

namespace nxui {

namespace {

constexpr float kScreenWidth = 1280.f;
constexpr float kScreenHeight = 720.f;
constexpr float kPointerCenterX = kScreenWidth * 0.5f;
constexpr float kPointerCenterY = kScreenHeight * 0.5f;
constexpr float kGyroDeadzone = 0.002f;
constexpr float kNavStickDisableThreshold = 0.30f;

float applyGyroDeadzone(float value) {
    if (std::abs(value) <= kGyroDeadzone)
        return 0.f;

    return value;
}

} // namespace

void Input::initialize() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();

    recenterVirtualPointer();
    m_prevUpdateTick = armGetSystemTick();

    m_hasHandheldSixAxis = R_SUCCEEDED(hidGetSixAxisSensorHandles(
        &m_handheldSixAxisHandle, 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
    m_hasFullKeySixAxis = R_SUCCEEDED(hidGetSixAxisSensorHandles(
        &m_fullKeySixAxisHandle, 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey));
    m_hasJoyDualSixAxis = R_SUCCEEDED(hidGetSixAxisSensorHandles(
        m_joyDualSixAxisHandles, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));

    if (m_hasHandheldSixAxis)
        hidStartSixAxisSensor(m_handheldSixAxisHandle);
    if (m_hasFullKeySixAxis)
        hidStartSixAxisSensor(m_fullKeySixAxisHandle);
    if (m_hasJoyDualSixAxis) {
        hidStartSixAxisSensor(m_joyDualSixAxisHandles[0]);
        hidStartSixAxisSensor(m_joyDualSixAxisHandles[1]);
    }
}

void Input::shutdown() {
    if (m_hasHandheldSixAxis) {
        hidStopSixAxisSensor(m_handheldSixAxisHandle);
        m_hasHandheldSixAxis = false;
    }
    if (m_hasFullKeySixAxis) {
        hidStopSixAxisSensor(m_fullKeySixAxisHandle);
        m_hasFullKeySixAxis = false;
    }
    if (m_hasJoyDualSixAxis) {
        hidStopSixAxisSensor(m_joyDualSixAxisHandles[0]);
        hidStopSixAxisSensor(m_joyDualSixAxisHandles[1]);
        m_hasJoyDualSixAxis = false;
    }

    m_virtualPointerEnabled = false;
}

void Input::recenterVirtualPointer() {
    m_virtualPointerX = kPointerCenterX;
    m_virtualPointerY = kPointerCenterY;
}

bool Input::readActiveSixAxisState(HidSixAxisSensorState& out) const {
    u32 styleSet = padGetStyleSet(&m_pad);
    if ((styleSet & HidNpadStyleTag_NpadHandheld) && m_hasHandheldSixAxis) {
        return hidGetSixAxisSensorStates(m_handheldSixAxisHandle, &out, 1) > 0;
    }

    if ((styleSet & HidNpadStyleTag_NpadFullKey) && m_hasFullKeySixAxis) {
        return hidGetSixAxisSensorStates(m_fullKeySixAxisHandle, &out, 1) > 0;
    }

    if ((styleSet & HidNpadStyleTag_NpadJoyDual) && m_hasJoyDualSixAxis) {
        u32 attributes = padGetAttributes(&m_pad);
        if ((attributes & HidNpadAttribute_IsRightConnected)
            && hidGetSixAxisSensorStates(m_joyDualSixAxisHandles[1], &out, 1) > 0) {
            return true;
        }
        if ((attributes & HidNpadAttribute_IsLeftConnected)
            && hidGetSixAxisSensorStates(m_joyDualSixAxisHandles[0], &out, 1) > 0) {
            return true;
        }
    }

    return false;
}

void Input::update() {
    uint64_t nowTick = armGetSystemTick();
    float dt = m_prevUpdateTick
        ? static_cast<float>(nowTick - m_prevUpdateTick) / static_cast<float>(armGetSystemTickFreq())
        : (1.f / 60.f);
    m_prevUpdateTick = nowTick;
    if (dt > 0.05f) dt = 1.f / 60.f;

    padUpdate(&m_pad);
    m_kDown = padGetButtonsDown(&m_pad);
    m_kUp   = padGetButtonsUp(&m_pad);
    m_kHeld = padGetButtons(&m_pad);

    // Analog sticks
    HidAnalogStickState ls = padGetStickPos(&m_pad, 0);
    HidAnalogStickState rs = padGetStickPos(&m_pad, 1);
    m_lx = ls.x / 32767.f;
    m_ly = ls.y / 32767.f;
    m_rx = rs.x / 32767.f;
    m_ry = rs.y / 32767.f;

    HidTouchScreenState tState = {};
    bool hardwareTouching = hidGetTouchScreenStates(&tState, 1) && tState.count > 0;
    float hardwareTouchX = hardwareTouching ? tState.touches[0].x : m_touchX;
    float hardwareTouchY = hardwareTouching ? tState.touches[0].y : m_touchY;

    bool navInputUsed =
        isDown(Button::DLeft)  || isDown(Button::DRight) ||
        isDown(Button::DUp)    || isDown(Button::DDown)  ||
        isDown(Button::LStickL)|| isDown(Button::LStickR)||
        isDown(Button::LStickU)|| isDown(Button::LStickD)||
        std::abs(m_lx) >= kNavStickDisableThreshold ||
        std::abs(m_ly) >= kNavStickDisableThreshold;

    if (hardwareTouching || navInputUsed) {
        m_virtualPointerEnabled = false;
    }

    if (isDown(Button::ZL)) {
        m_virtualPointerEnabled = !m_virtualPointerEnabled;
    }
    if (isDown(Button::ZR)) {
        recenterVirtualPointer();
    }

    if (m_virtualPointerEnabled) {
        HidSixAxisSensorState sixAxis = {};
        if (readActiveSixAxisState(sixAxis)) {
            float roll = applyGyroDeadzone(sixAxis.angular_velocity.z);
            float pitch = applyGyroDeadzone(sixAxis.angular_velocity.x);

            m_virtualPointerX = std::clamp(
                m_virtualPointerX - roll * m_virtualPointerSensitivity * dt,
                0.f, kScreenWidth - 1.f);
            m_virtualPointerY = std::clamp(
                m_virtualPointerY - pitch * m_virtualPointerSensitivity * dt,
                0.f, kScreenHeight - 1.f);
        }
    }

    m_wasTouching = m_touching;
    if (hardwareTouching) {
        m_touchX = hardwareTouchX;
        m_touchY = hardwareTouchY;
    } else if (m_virtualPointerEnabled) {
        m_touchX = m_virtualPointerX;
        m_touchY = m_virtualPointerY;
    }

    bool virtualTouching = !hardwareTouching
        && m_virtualPointerEnabled
        && (m_kHeld & static_cast<uint64_t>(Button::A));

    m_touching = hardwareTouching || virtualTouching;

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

} // namespace nxui
