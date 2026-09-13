#pragma once
#include <nxui/widgets/Box.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <memory>

namespace nxui {

class Application;
class Renderer;

/// Abstract base for an application screen / activity.
/// Attach to an Application via setActivity() -- the Application
/// manages GPU, Renderer, Input and the main loop.
///
/// Every Activity owns a rootBox (a full-screen Box with COLUMN axis)
/// which is the root of the widget tree. Add UI elements as children
/// of rootBox(). The framework automatically updates and renders
/// the tree each frame.
///
/// The Activity also owns a FocusManager that provides:
///  - Debounced D-pad → spatial navigate()
///  - Action dispatch with parent-chain bubbling
///  - A-button → activate() on focused widget
/// Override focusRoot() to redirect focus when overlays are active.
class Activity {
public:
    virtual ~Activity() = default;

    /// Optional first paint before onCreate(). Return true if this drew
    /// something meaningful (e.g. a leave-frame splash) so Application can
    /// skip the default black clear frame.
    virtual bool presentInitialFrame(Renderer& /*ren*/) { return false; }

    /// Called once after GPU / Renderer / Input are ready.
    virtual bool onCreate() { return true; }

    /// Called before GPU / Renderer shutdown.
    virtual void onDestroy() {}

    /// Called each frame. Input has already been updated.
    virtual void onUpdate(float dt) {}

    /// Called each frame between beginFrame / endFrame.
    /// The rootBox is rendered before this, so anything drawn here
    /// appears on top of the widget tree (useful for overlays).
    virtual void onRender(Renderer& ren) {}

    /// Called after a frame has been submitted/presented. Safe place for
    /// GPU→CPU framebuffer readback without interrupting the render pass.
    virtual void onAfterPresent(Renderer& /*ren*/) {}

    /// Access the parent Application (set by Application::setActivity).
    Application& app() { return *m_app; }
    const Application& app() const { return *m_app; }

    /// The root layout box. Add all UI elements as children of this box.
    /// Automatically updated and rendered each frame by the framework.
    Box& rootBox() { return *m_rootBox; }
    const Box& rootBox() const { return *m_rootBox; }

    /// The focus manager for the activity.
    FocusManager& focusManager() { return m_focusManager; }
    const FocusManager& focusManager() const { return m_focusManager; }

    /// The widget subtree used for focus navigation this frame.
    /// Override to redirect to an overlay when it is active.
    /// Return nullptr to block all input dispatch for this frame.
    virtual Widget* focusRoot() { return m_rootBox.get(); }

private:
    friend class Application;
    Application* m_app = nullptr;
    std::shared_ptr<Box> m_rootBox = std::make_shared<Box>(Axis::COLUMN);
    FocusManager m_focusManager;
};

} // namespace nxui
