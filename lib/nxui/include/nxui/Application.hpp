#pragma once
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <functional>
#include <memory>

namespace nxui {

class Activity;

/// Main-loop timing, reported every kFrameStatsWindowSec and for single slow frames.
struct FrameStats {
    bool     slowFrame   = false; // one frame over budget, not a window summary
    uint32_t frames      = 0;     // presented frames in the window
    float    seconds     = 0.f;   // window length
    float    avgMs       = 0.f;   // average frame interval
    float    maxMs       = 0.f;   // worst frame interval
    uint32_t missed      = 0;     // intervals that missed their vblank
    float    maxUpdateMs = 0.f;   // input + activity/widget update
    float    maxRenderMs = 0.f;   // record + submit + present (vblank wait excluded)
    uint32_t idleFrames  = 0;     // frames presented at the idle half rate
};

/// Top-level application object.
/// Owns the GPU device, Renderer, and Input, runs the main loop,
/// and delegates lifecycle events to the attached Activity.
///
/// Usage:
///   nxui::Application app;
///   app.setActivity(std::make_unique<MyActivity>());
///   if (app.initialize()) app.run();
///   app.shutdown();
class Application {
public:
    Application() = default;
    ~Application();

    /// Attach the main activity. Must be called before initialize().
    void setActivity(std::unique_ptr<Activity> activity);

    /// Queue an activity change from inside the main loop.
    void requestActivity(std::unique_ptr<Activity> activity);

    /// Initialise GPU, Renderer, Input, then call activity->onCreate().
    bool initialize();

    /// Enter the main loop (blocks until requestExit() is called).
    void run();

    /// Shutdown: activity->onDestroy(), then GPU/Renderer cleanup.
    void shutdown();

    // Accessors used by Activity
    GpuDevice& gpu()       { return m_gpu; }
    Renderer&  renderer()  { return *m_renderer; }
    Input&     input()     { return m_input; }

    void requestExit()       { m_running = false; }
    bool isRunning() const   { return m_running; }

    /// Disable/enable GPU rendering.  When disabled the main loop still
    /// runs input + update, but skips beginFrame/endFrame so the GPU is
    /// free for whichever app currently owns the foreground.
    void setRenderEnabled(bool e) { m_renderEnabled = e; }
    bool renderEnabled() const    { return m_renderEnabled; }

    /// Low-power console sleep.  No frame is presented, no input is dispatched
    /// and the loop parks on a 50 ms tick; the activity is still updated slowly
    /// so it can read the wake notification and resume.
    void setSleeping(bool s) { m_sleeping = s; }
    bool sleeping() const    { return m_sleeping; }

    /// Receives frame timing (called on the main thread).
    void setFrameStatsSink(std::function<void(const FrameStats&)> sink) {
        m_frameStatsSink = std::move(sink);
    }

private:
    std::function<void(const FrameStats&)> m_frameStatsSink;
    void dispatchInput();
    bool applyPendingActivity();

    GpuDevice  m_gpu;
    std::unique_ptr<Renderer> m_renderer;
    Input      m_input;

    std::unique_ptr<Activity> m_activity;
    std::unique_ptr<Activity> m_pendingActivity;
    bool m_running = true;
    bool m_renderEnabled = true;
    bool m_sleeping = false;
    uint64_t m_navHeldMask = 0;
    int m_navHeldFrames = 0;
    int m_navRepeatCountdown = 0;
    int m_navSinceStepFrames = 1000;
};

} // namespace nxui
