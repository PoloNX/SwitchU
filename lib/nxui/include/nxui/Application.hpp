#pragma once
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <memory>

namespace nxui {

class Activity;

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

private:
    void dispatchInput();

    GpuDevice  m_gpu;
    std::unique_ptr<Renderer> m_renderer;
    Input      m_input;

    std::unique_ptr<Activity> m_activity;
    bool m_running = true;
    bool m_renderEnabled = true;
    int  m_navDebounce = 0;
};

} // namespace nxui
