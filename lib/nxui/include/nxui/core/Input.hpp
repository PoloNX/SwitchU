#pragma once
#include <nxui/core/DisplayOrientation.hpp>
#include <switch.h>
#include <cstdint>

namespace nxui {

enum class Button : uint64_t {
    A      = HidNpadButton_A,
    B      = HidNpadButton_B,
    X      = HidNpadButton_X,
    Y      = HidNpadButton_Y,
    Plus   = HidNpadButton_Plus,
    Minus  = HidNpadButton_Minus,
    DLeft  = HidNpadButton_Left,
    DRight = HidNpadButton_Right,
    DUp    = HidNpadButton_Up,
    DDown  = HidNpadButton_Down,
    L      = HidNpadButton_L,
    R      = HidNpadButton_R,
    ZL     = HidNpadButton_ZL,
    ZR     = HidNpadButton_ZR,
    LStick = HidNpadButton_StickL,
    RStick = HidNpadButton_StickR,
    LStickL = HidNpadButton_StickLLeft,
    LStickR = HidNpadButton_StickLRight,
    LStickU = HidNpadButton_StickLUp,
    LStickD = HidNpadButton_StickLDown,
    RStickL = HidNpadButton_StickRLeft,
    RStickR = HidNpadButton_StickRRight,
    RStickU = HidNpadButton_StickRUp,
    RStickD = HidNpadButton_StickRDown,
};

class Input {
public:
    void initialize();
    void shutdown();
    void update();

    // Flip-Grip / TATE: remap touch + directionals for the active layout.
    // Landscape/180 → FB space (nwindow). Portrait 90/270 → logical 720x1280
    // (Renderer projection; nwindow identity).
    void setDisplayOrientation(DisplayOrientation orientation);
    DisplayOrientation displayOrientation() const { return m_orientation; }
    void setLogicalLayoutSize(float width, float height);
    float logicalLayoutWidth() const { return m_logicalW; }
    float logicalLayoutHeight() const { return m_logicalH; }

    bool isDown(Button b)     const;   // Pressed this frame
    bool isUp(Button b)       const;   // Released this frame
    bool isHeld(Button b)     const;   // Currently held

    // Analog sticks (normalized −1..+1)
    float leftStickX()  const { return m_lx; }
    float leftStickY()  const { return m_ly; }
    float rightStickX() const { return m_rx; }
    float rightStickY() const { return m_ry; }

    // Touch screen (first touch point)
    bool  isTouching()   const { return m_touching; }
    bool  touchDown()    const { return m_touchDown; }   // Just started touching
    bool  touchUp()      const { return m_touchUp; }     // Just released
    float touchX()       const { return m_touchX; }
    float touchY()       const { return m_touchY; }
    float touchStartX()  const { return m_touchStartX; } // Where the touch began
    float touchStartY()  const { return m_touchStartY; }
    float touchDeltaX()  const { return m_touchX - m_touchStartX; }
    float touchDeltaY()  const { return m_touchY - m_touchStartY; }
    float touchDuration() const;  // seconds since touch began (valid on touchUp)

    bool  hasUserActivity() const;

private:
    uint64_t m_kDown = 0;
    uint64_t m_kUp   = 0;
    uint64_t m_kHeld = 0;
    uint32_t m_loggedSingleJoyCons = 0;  // bit per npad id, logged once
    DisplayOrientation m_orientation = DisplayOrientation::Landscape0;
    float m_logicalW = 1280.f;
    float m_logicalH = 720.f;
    float m_lx = 0, m_ly = 0;
    float m_rx = 0, m_ry = 0;
    bool  m_touching    = false;
    bool  m_wasTouching = false;
    bool  m_touchDown   = false;
    bool  m_touchUp     = false;
    float m_touchX = 0, m_touchY = 0;
    float m_touchStartX = 0, m_touchStartY = 0;
    uint64_t m_touchStartTick = 0;
};

} // namespace nxui
