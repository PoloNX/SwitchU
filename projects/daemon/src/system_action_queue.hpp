#pragma once

#include <switch.h>

#include <cstddef>
#include <cstdint>
#include <deque>

namespace switchu::daemon {

enum class SystemActionType : std::uint32_t {
    LaunchApplication,
    ResumeApplication,
    OpenAlbum,
    OpenMiiEditor,
    OpenControllers,
    OpenControllerRemapping,
    OpenNetConnect,
    OpenUserPage,
    OpenUserCreator,
};

struct SystemAction {
    SystemActionType type = SystemActionType::LaunchApplication;
    std::uint64_t requestId = 0;
    std::uint64_t titleId = 0;
    AccountUid uid{};
};

// The menu is an all-foreground applet: accepting multiple navigation actions
// while it is closing makes the second one execute against a later menu
// session. Keep exactly one pending transition and reject stale double input.
class SystemActionQueue final {
public:
    static constexpr std::size_t kMaxPending = 1;
    static constexpr Result kBusyResult = MAKERESULT(Module_Libnx, 0x1FD);

    Result enqueue(const SystemAction& action) {
        if (m_items.size() >= kMaxPending)
            return kBusyResult;
        m_items.push_back(action);
        return 0;
    }

    bool pop(SystemAction& out) {
        if (m_items.empty())
            return false;
        out = m_items.front();
        m_items.pop_front();
        return true;
    }

    bool empty() const { return m_items.empty(); }
    std::size_t size() const { return m_items.size(); }
    void clear() { m_items.clear(); }

private:
    std::deque<SystemAction> m_items;
};

} // namespace switchu::daemon
