#pragma once
#include <functional>
#include <atomic>
#include <switch.h>

class AppletLauncher {
public:
    struct Callbacks {
        std::function<void()>     playSfxModalHide;
        std::function<void()>     playSfxLaunchGame;
        std::function<void()>     requestExit;
        std::function<void()>     suspendForApp;
#ifdef SWITCHU_STANDALONE
        // Request the standalone manager to launch a library applet (Album / Mii / NetConnect).
        std::function<void(AppletId)> launchLibraryApplet;
#endif
        std::function<void()>     waitGpuIdle;
        std::function<void(bool)> setRenderEnabled;
    };

    void init(Callbacks cbs);

    void launchAlbum();
    void launchMiiEditor();
    void launchControllerPairing();
    void launchNetConnect();
    void enterSleep();

    void launchApplication(uint64_t titleId, AccountUid uid);
    void resumeApplication();
    void terminateApplication();

    void checkRunningApplication();

    bool     isAppRunning()  const;
    bool     isAppSuspended(uint64_t titleId) const;
    uint64_t suspendedTitleId() const;

    void setAppRunning(bool v);
    void setAppHasForeground(bool v);
    void setSuspendedTitleId(uint64_t v);

#ifdef SWITCHU_MENU
    void setStartupStatus(uint64_t suspendedTitleId, bool appRunning);
#endif

private:
#ifdef SWITCHU_MENU
    std::atomic<bool>     m_appRunning{false};
    std::atomic<bool>     m_appHasForeground{false};
    std::atomic<uint64_t> m_suspendedTitleId{0};
#endif

    Callbacks m_cb;
};
