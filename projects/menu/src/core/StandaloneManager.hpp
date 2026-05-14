#pragma once
#ifdef SWITCHU_STANDALONE

#include <switch.h>
#include <functional>
#include <atomic>
#include <cstdint>

// StandaloneManager – drives the system-applet event loop for the standalone build.
//
// In the normal daemon+menu split, the daemon owns:
//   - applet message handling (HOME, sleep, shutdown)
//   - General Channel (SAMS) handling
//   - application lifecycle via AppletApplication
//   - the NS application-record event thread
//
// In standalone mode all of this lives in the menu process.
// StandaloneManager encapsulates it cleanly and exposes callbacks so that
// WiiUMenuApp can react without knowing any libnx specifics.

struct StandaloneCallbacks {
    // HOME pressed while a game is running.  tid = suspended title id.
    std::function<void(uint64_t tid)> onAppSuspended;
    // Running application exited normally.
    std::function<void()>             onAppExited;
    // NS application-record changed (install / uninstall / update).
    std::function<void()>             onAppRecordsChanged;
    // Game-card mount failure; rc is the libnx result code.
    std::function<void(uint32_t rc)>  onGcMountFailure;
    // HOME pressed while the menu itself is in the foreground (no game running).
    std::function<void()>             onHomeRequest;
    // A library applet (Album, Mii Editor, NetConnect) is about to get foreground.
    std::function<void()>             onLibAppletStarted;
    // A library applet returned; menu should reclaim the screen.
    std::function<void()>             onLibAppletReturned;
};

class StandaloneManager {
public:
    void setCallbacks(StandaloneCallbacks cbs);

    // Start the background NS event-watcher thread.
    bool start();
    // Stop the background thread and close any open library applet holder.
    void stop();

    // Drive the per-frame system-event logic.  Call once from WiiUMenuApp::onUpdate().
    void update();

    // Request a library applet launch (fires onLibAppletStarted, then waits
    // frame-by-frame for the applet to finish, then fires onLibAppletReturned).
    void requestLaunchAlbum();
    void requestLaunchMiiEditor();
    void requestLaunchNetConnect();

    bool isLibAppletActive() const { return m_libHolderActive; }

private:
    void handleGeneralChannel();
    void handleAppletMessages();
    void launchPendingApplet();
    void checkLibraryAppletFinished();

    static void eventThreadFunc(void* arg);

    StandaloneCallbacks m_cb;

    Thread            m_eventThread{};
    std::atomic<bool> m_eventRunning{false};
    std::atomic<bool> m_recordEventPending{false};
    std::atomic<bool> m_gcMountFailure{false};
    std::atomic<uint32_t> m_gcMountRc{0};
    bool m_initialEventSkipped = false;

    enum class PendingApplet { None, Album, MiiEditor, NetConnect };
    PendingApplet m_pendingApplet    = PendingApplet::None;
    AppletHolder  m_libHolder        = {};
    bool          m_libHolderActive  = false;
    bool          m_foregroundAppletHome = false;
};

#endif // SWITCHU_STANDALONE
