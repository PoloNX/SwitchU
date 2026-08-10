#include "ManagerActivity.hpp"

#include <nxui/Application.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <switchu/file_log.hpp>
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <memory>

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
