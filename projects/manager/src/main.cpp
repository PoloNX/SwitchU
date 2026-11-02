#include "ManagerActivity.hpp"

#include <nxui/Application.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <switchu/file_log.hpp>
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <filesystem>
#include <memory>
#include <system_error>

extern "C" {
    size_t __nx_heap_size = 0x06000000;
}

extern "C" void userAppInit(void) {
    romfsInit();
    timeInitialize();
    setInitialize();
}

extern "C" void userAppExit(void) {
    setExit();
    timeExit();
    romfsExit();
}

int main(int, char**) {
    switchu::FileLog::open("manager");
    switchu::FileLog::log("[main] SwitchU Manager starting");

    // Best-effort: apply a Manager NRO staged by a previous update while this
    // process was running. May fail if the live NRO is still locked; daemon
    // promote after reboot is the reliable path.
    {
        namespace fs = std::filesystem;
        const fs::path live =
            "sdmc:/switch/SwitchU-Manager/SwitchU-Manager.nro";
        const fs::path pending =
            "sdmc:/switch/SwitchU-Manager/SwitchU-Manager.nro.new";
        std::error_code promoteEc;
        if (fs::is_regular_file(pending, promoteEc)) {
            const fs::path backup = live.string() + ".old";
            fs::remove(backup, promoteEc);
            promoteEc.clear();
            fs::rename(live, backup, promoteEc);
            promoteEc.clear();
            fs::rename(pending, live, promoteEc);
            if (promoteEc) {
                switchu::FileLog::log(
                    "[main] pending Manager promote failed ec=%d (daemon will retry)",
                    promoteEc.value());
                // Restore live if we moved it aside but couldn't finalize.
                promoteEc.clear();
                if (!fs::exists(live) && fs::exists(backup))
                    fs::rename(backup, live, promoteEc);
            } else {
                fs::remove(backup, promoteEc);
                fsdevCommitDevice("sdmc");
                switchu::FileLog::log("[main] pending Manager NRO promoted");
            }
        }
    }

    SDL_Init(0);
    TTF_Init();
    nxui::Renderer::setShaderBasePath("romfs:/shaders/");
    nxui::I18n::instance().initialize("romfs:/i18n", "en-US");

    nxui::Application application;
    application.setActivity(std::make_unique<switchu::manager::ManagerActivity>());
    const bool initialized = application.initialize();
    if (initialized)
        application.run();
    else
        switchu::FileLog::log("[main] nxui initialization failed");
    application.shutdown();

    TTF_Quit();
    SDL_Quit();
    switchu::FileLog::close();
    return initialized ? 0 : 1;
}
