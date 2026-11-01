#include <nxui/Application.hpp>
#include <nxui/Activity.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <switch.h>
#include <algorithm>
#include <cmath>

namespace nxui {

Application::~Application() {
    shutdown();
}

void Application::setActivity(std::unique_ptr<Activity> activity) {
    m_activity = std::move(activity);
    if (m_activity) m_activity->m_app = this;
}

void Application::requestActivity(std::unique_ptr<Activity> activity) {
    m_pendingActivity = std::move(activity);
    if (m_pendingActivity)
        m_pendingActivity->m_app = this;
}

bool Application::applyPendingActivity() {
    if (!m_pendingActivity)
        return true;

    AnimationManager::instance().clear();
    if (m_activity)
        m_activity->onDestroy();

    m_activity = std::move(m_pendingActivity);
    m_navHeldMask = 0;
    m_navHeldFrames = 0;
    m_navRepeatCountdown = 0;

    if (m_activity) {
        m_activity->m_rootBox->setRect({0, 0, (float)m_gpu.width(), (float)m_gpu.height()});
        return m_activity->onCreate();
    }
    return true;
}

bool Application::initialize() {
    if (!m_gpu.initialize()) return false;

    m_renderer = std::make_unique<Renderer>(m_gpu);
    if (!m_renderer->initialize()) return false;

    m_input.initialize();

    // Present one clean frame immediately so that stale framebuffer
    // content from a previous process is never visible on screen.
    // Activities may replace the default black with a leave-frame splash.
    m_gpu.beginFrame();
    m_renderer->beginFrame();
    if (m_activity)
        m_activity->presentInitialFrame(*m_renderer);
    m_renderer->endFrame();
    m_gpu.endFrame();

    if (m_activity) {
        // Set the root box to cover the entire screen
        m_activity->m_rootBox->setRect({0, 0, (float)m_gpu.width(), (float)m_gpu.height()});
        if (!m_activity->onCreate()) return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
// Automatic input dispatch
// ══════════════════════════════════════════════════════════════════════════════

void Application::dispatchInput() {
    if (!m_activity) return;

    Widget* root = m_activity->focusRoot();
    if (!root) return;      // nullptr = all input blocked this frame

    auto& fm = m_activity->focusManager();

    // Keep focus constrained to the currently active input root.
    // Without this, stale focus from another UI layer (e.g. home grid while
    // settings/dialog is active) can still receive A/button dispatches.
    auto isUnderRoot = [root](Widget* w) {
        for (Widget* it = w; it != nullptr; it = it->parent()) {
            if (it == root) return true;
        }
        return false;
    };
    Widget* curFocus = fm.current();
    if (!curFocus || !isUnderRoot(curFocus)) {
        if (root->isFocusable()) {
            fm.setFocus(root);
        } else {
            std::vector<Widget*> focusables;
            root->collectFocusable(focusables);
            if (!focusables.empty())
                fm.setFocus(focusables[0]);
        }
    }

    // Debounced D-pad and stick navigation.
    // For each direction: if the focused widget has an action for that
    // D-pad/stick button, fire it (consumed). Otherwise navigate spatially.
    constexpr uint64_t kNavMask =
        static_cast<uint64_t>(Button::DLeft)   | static_cast<uint64_t>(Button::DRight)  |
        static_cast<uint64_t>(Button::DUp)     | static_cast<uint64_t>(Button::DDown)   |
        static_cast<uint64_t>(Button::LStickL) | static_cast<uint64_t>(Button::LStickR) |
        static_cast<uint64_t>(Button::LStickU) | static_cast<uint64_t>(Button::LStickD) |
        static_cast<uint64_t>(Button::RStickL) | static_cast<uint64_t>(Button::RStickR) |
        static_cast<uint64_t>(Button::RStickU) | static_cast<uint64_t>(Button::RStickD);
    uint64_t heldMask = 0;
    uint64_t pressedMask = 0;
    for (Button button : {Button::DLeft, Button::DRight, Button::DUp, Button::DDown,
                          Button::LStickL, Button::LStickR, Button::LStickU, Button::LStickD,
                          Button::RStickL, Button::RStickR, Button::RStickU, Button::RStickD}) {
        const uint64_t bit = static_cast<uint64_t>(button);
        if (m_input.isHeld(button)) heldMask |= bit;
        if (m_input.isDown(button)) pressedMask |= bit;
    }

    constexpr uint64_t kStickMask =
        static_cast<uint64_t>(Button::LStickL) | static_cast<uint64_t>(Button::LStickR) |
        static_cast<uint64_t>(Button::LStickU) | static_cast<uint64_t>(Button::LStickD) |
        static_cast<uint64_t>(Button::RStickL) | static_cast<uint64_t>(Button::RStickR) |
        static_cast<uint64_t>(Button::RStickU) | static_cast<uint64_t>(Button::RStickD);
    // Circling the stick changes the held direction every few frames and each
    // change used to be an immediate step. For the stick alone, a direction
    // change only moves again after a short gap since the last step.
    constexpr int kStickMinStepFrames = 10;
    if (m_navSinceStepFrames < 1000) ++m_navSinceStepFrames;
    const bool stickOnly = heldMask != 0 && (heldMask & ~kStickMask) == 0;

    bool dispatchNavigation = false;
    if (heldMask == 0) {
        m_navHeldMask = 0;
        m_navHeldFrames = 0;
        m_navRepeatCountdown = 0;
    } else if ((pressedMask != 0 || heldMask != m_navHeldMask) && stickOnly &&
               m_navSinceStepFrames < kStickMinStepFrames) {
        m_navHeldMask = heldMask;
    } else if (pressedMask != 0 || heldMask != m_navHeldMask) {
        // The first step is immediate, followed by a deliberate pause. This
        // keeps a normal press precise and makes the start of a hold calmer.
        m_navHeldMask = heldMask;
        m_navHeldFrames = 0;
        m_navRepeatCountdown = 28;
        dispatchNavigation = true;
    } else {
        ++m_navHeldFrames;
        if (m_navRepeatCountdown > 0)
            --m_navRepeatCountdown;
        if (m_navRepeatCountdown == 0) {
            dispatchNavigation = true;
            // Accelerate one step per second from ~5 moves/s to ~10 moves/s.
            // The cap leaves enough time for artwork streaming to catch up.
            // A held analog stick is much easier to overshoot with than the
            // D-pad, so it repeats about 1.6x slower (~4 to ~7 moves/s).
            m_navRepeatCountdown = (heldMask & ~kStickMask) == 0
                ? std::clamp(19 - m_navHeldFrames / 60, 10, 19)
                : std::clamp(12 - m_navHeldFrames / 60, 6, 12);
        }
    }

    if (dispatchNavigation) {
        m_navSinceStepFrames = 0;

        Widget* cur = fm.current();

        // Hardware stick "buttons" light up on mild diagonals. Prefer the
        // dominant axis from the analog sample so a mostly-horizontal flick
        // with a little upward drift does not also step into top chrome.
        // D-pad is unaffected (always deliberate).
        // Ratio > 1 means vertical must clearly beat horizontal to count as UP.
        constexpr float kStickAxisDominance = 1.55f;
        const float lx = m_input.leftStickX();
        const float ly = m_input.leftStickY();
        const float rx = m_input.rightStickX();
        const float ry = m_input.rightStickY();
        const float ax = (std::abs(lx) >= std::abs(rx)) ? lx : rx;
        const float ay = (std::abs(ly) >= std::abs(ry)) ? ly : ry;
        const float absX = std::abs(ax);
        const float absY = std::abs(ay);
        const bool stickHorizOk = absX >= absY / kStickAxisDominance;
        const bool stickVertOk  = absY >= absX * kStickAxisDominance;

        auto tryDir = [&](Button dpad, Button leftStick, Button rightStick, FocusDirection dir) {
            bool dpadDown  = m_input.isHeld(dpad);
            bool leftStickDown = m_input.isHeld(leftStick);
            bool rightStickDown = m_input.isHeld(rightStick);

            const bool vertical = (dir == FocusDirection::UP || dir == FocusDirection::DOWN);
            if (vertical) {
                if (!stickVertOk) {
                    leftStickDown = false;
                    rightStickDown = false;
                }
            } else if (!stickHorizOk) {
                leftStickDown = false;
                rightStickDown = false;
            }

            if (!dpadDown && !leftStickDown && !rightStickDown) return;

            // Focused widget's action takes priority (no bubbling for D-pad)
            if (cur) {
                if (dpadDown && cur->fireAction(static_cast<uint64_t>(dpad))) return;
                if (leftStickDown && cur->fireAction(static_cast<uint64_t>(leftStick))) return;
                if (rightStickDown && cur->fireAction(static_cast<uint64_t>(rightStick))) return;
            }
            // Activity may swallow spatial D-pad steps during HOME overlay chords
            // (Ultrahand L+R+Y+D-pad) without delaying solo stick browsing.
            if (dpadDown && m_activity && !m_activity->allowDpadFocusNavigation(dpad))
                dpadDown = false;
            if (!dpadDown && !leftStickDown && !rightStickDown) return;
            fm.navigate(dir, root);
        };

        tryDir(Button::DLeft,  Button::LStickL, Button::RStickL, FocusDirection::LEFT);
        tryDir(Button::DRight, Button::LStickR, Button::RStickR, FocusDirection::RIGHT);
        tryDir(Button::DUp,    Button::LStickU, Button::RStickU, FocusDirection::UP);
        tryDir(Button::DDown,  Button::LStickD, Button::RStickD, FocusDirection::DOWN);
    }

    // Dispatch non-D-pad actions with parent bubbling.
    // Exclude D-pad buttons so they aren't fired a second time.
    constexpr uint64_t kA = static_cast<uint64_t>(Button::A);
    uint64_t actionExcludeMask = kNavMask;

    uint64_t consumed = fm.dispatchActions(m_input, actionExcludeMask);

    // Auto-activate with A.
    // If the focused widget didn't register an explicit addAction(A, ...),
    // fall through to the legacy activate() / setOnActivate() mechanism.
    if (!(consumed & kA) && m_input.isDown(Button::A)) {
        if (auto* w = fm.current())
            w->activate();
    }

    // Touch-based focus navigation.
    // Some screens own richer touch handling locally (drag/scroll/menus) and
    // should not also receive the generic focus-manager tap model.
    if (root->frameworkTouchEnabled())
        fm.handleTouch(m_input, root);
}

// ══════════════════════════════════════════════════════════════════════════════
// Main loop
// ══════════════════════════════════════════════════════════════════════════════

void Application::run() {
    uint64_t prevTick = armGetSystemTick();
    float idleSeconds = 0.f;
    bool skipIdleFrame = false;

    while (m_running) {
        uint64_t nowTick = armGetSystemTick();
        float dt = static_cast<float>(nowTick - prevTick)
                 / static_cast<float>(armGetSystemTickFreq());
        prevTick = nowTick;
        if (dt > 0.1f) dt = 0.016f;

        if (m_sleeping) {
            // The console is asleep: nothing is on screen, so stop submitting
            // frames and stop touching input. The activity still gets a slow
            // tick because that is where the sleep/wake notification from the
            // daemon is read; anything else it does is skipped on its side.
            if (m_activity)
                m_activity->onUpdate(dt);
            // If onUpdate cleared sleeping (wake), fall through and present
            // immediately — don't burn another 50ms before the first frame.
            if (m_sleeping) {
                svcSleepThread(50'000'000ULL);
                prevTick = armGetSystemTick();
                continue;
            }
            prevTick = armGetSystemTick();
        }

        m_input.update();
        if (m_input.hasUserActivity()) {
            idleSeconds = 0.f;
            skipIdleFrame = false;
        } else {
            idleSeconds += dt;
        }
        dispatchInput();

        if (m_activity) {
            m_activity->onUpdate(dt);
            if (!applyPendingActivity()) {
                m_running = false;
                break;
            }
            m_activity->m_rootBox->update(dt);

            if (m_renderEnabled) {
                // Static menus do not need a full GPU submission at 60 Hz.
                // Keep input/update responsive, but present every other frame
                // after the UI has been idle long enough for transitions to
                // settle. Any new input immediately restores 60 Hz.
                if (idleSeconds >= 2.f) {
                    skipIdleFrame = !skipIdleFrame;
                    if (skipIdleFrame) {
                        svcSleepThread(16'000'000ULL);
                        continue;
                    }
                }
                m_gpu.beginFrame();
                m_renderer->beginFrame();
                m_activity->m_rootBox->render(*m_renderer);
                m_activity->onRender(*m_renderer);
                m_renderer->endFrame();
                m_gpu.endFrame();
                m_activity->onAfterPresent(*m_renderer);
            } else {
                // Yield CPU while another app owns the foreground.
                svcSleepThread(100000000LL); // 100 ms
            }
        }
    }
}

void Application::shutdown() {
    // Clear all pending animations before destroying the activity so that
    // tween callbacks don't fire on already-destroyed widgets.
    AnimationManager::instance().clear();

    m_input.shutdown();

    if (m_activity) {
        m_activity->onDestroy();
        m_activity.reset();
    }
    m_pendingActivity.reset();
    m_renderer.reset();
    m_gpu.shutdown();
}

} // namespace nxui
