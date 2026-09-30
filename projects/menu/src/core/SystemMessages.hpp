#pragma once
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <switch.h>

#ifdef SWITCHU_MENU
enum class SysAction {
    HomeButton,   // return from title / applet (keep open folder)
    HomeDismiss,  // HOME while already browsing — close folder overlays
    Sleep,
    Shutdown,
    Reboot,
    OperationModeChanged,
};
#endif

class SystemMessages {
public:
#ifdef SWITCHU_MENU
    using ActionCallback = std::function<void(SysAction)>;

    void setCallback(ActionCallback cb) { m_callback = std::move(cb); }

    void start();
    void stop();

    void pushAction(SysAction a);

    void pump();

private:
    ActionCallback              m_callback;
    std::mutex                  m_mutex;
    std::vector<SysAction>      m_pending;
#endif
};
