#pragma once
#include <functional>
#include <atomic>
#include <switch.h>

class AppletLauncher {
public:
    struct Callbacks {
        std::function<void()>     playSfxModalHide;
        std::function<void()>     requestExit;
        std::function<bool()>     beforePowerAction;
    };

    void init(Callbacks cbs);

    void launchAlbum();
    void launchMiiEditor();
    void launchControllerPairing();
    void launchControllerRemapping();
    void launchNetConnect();
    void launchUserPage(AccountUid uid);
    void enterSleep();
    void shutdown();
    void reboot();

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
    bool preparePowerAction();
#endif
#ifdef SWITCHU_MENU
    std::atomic<bool>     m_appRunning{false};
    std::atomic<bool>     m_appHasForeground{false};
    std::atomic<uint64_t> m_suspendedTitleId{0};
#endif

    Callbacks m_cb;
};
