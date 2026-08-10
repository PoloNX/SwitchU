#pragma once

#include <cstdint>

namespace switchu::navigation {

enum class Route : std::uint8_t {
    Home,
    Settings,
    ThemeShop,
    GameOptions,
    FolderOptions,
    ControllerTest,
};

// Authoritative owner of primary-screen input. Visual exit animations may
// overlap, but only the current route is allowed to receive focus.
class MenuNavigator final {
public:
    Route route() const { return m_route; }
    std::uint64_t generation() const { return m_generation; }

    void navigate(Route target) {
        if (m_route == target)
            return;
        m_route = target;
        ++m_generation;
    }

    void resetToHome() { navigate(Route::Home); }

    void routeDidClose(Route closedRoute) {
        if (m_route == closedRoute)
            resetToHome();
    }

private:
    Route m_route = Route::Home;
    std::uint64_t m_generation = 1;
};

} // namespace switchu::navigation
