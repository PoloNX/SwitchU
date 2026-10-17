
#include "core/WiiUMenuApp.hpp"
#include "core/DebugLog.hpp"
#include "core/Config.hpp"
#include "tutorial/TutorialActivity.hpp"
#include "screen_fit.hpp"
#include <nxui/Application.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <fmt/format.h>
#ifdef SWITCHU_MENU
#include <nxui/core/Renderer.hpp>
#include <switchu/smi_protocol.hpp>
#include <switchu/file_log.hpp>
#endif
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#ifdef SWITCHU_MENU
#include <cstring>
#endif
#include <memory>

namespace {
constexpr size_t kMenuAppletHeapSize = 224u * 1024u * 1024u;
}

extern "C" {
#ifdef SWITCHU_HOMEBREW
    u32 __nx_applet_type = AppletType_Application;

    size_t __nx_heap_size = 0xD000000;
#else
    u32 __nx_applet_type = AppletType_LibraryApplet;
    u32 __nx_fs_num_sessions = 1;

    size_t __nx_heap_size = kMenuAppletHeapSize;

    TimeServiceType __nx_time_service_type = TimeServiceType_Menu;

    void __libnx_init_time(void);

    void __nx_win_init(void);
    void __nx_win_exit(void);

    // Weak in libnx (0): __nx_win_init opens this layer instead of creating one.
    extern u64 __nx_vi_layer_id;
#endif
}

namespace switchu::menu {

#ifdef SWITCHU_HOMEBREW
bool initScreenFitLayer(std::uint64_t) { return false; }
bool applyScreenUnderscan() { return true; }
#else
namespace {
u64 g_managedLayerId = 0;
}

bool initScreenFitLayer(std::uint64_t managedLayerId) {
    if (managedLayerId == 0)
        return false;
    g_managedLayerId = managedLayerId;
    return applyScreenUnderscan();
}

bool applyScreenUnderscan() {
    if (g_managedLayerId == 0)
        return false;

    // Underscan is a TV concern: in handheld the layer is already exactly
    // 1280x720 and must stay untouched.
    if (appletGetOperationMode() != AppletOperationMode_Console)
        return true;

    constexpr s32 kFullW = 1920;
    constexpr s32 kFullH = 1080;

    u32 underscan = 100;
    SetSysTvSettings tv{};
    if (R_SUCCEEDED(setsysGetTvSettings(&tv)) && tv.underscan >= 1 && tv.underscan <= 100)
        underscan = tv.underscan;

    const s32 w = kFullW * static_cast<s32>(underscan) / 100;
    const s32 h = kFullH * static_cast<s32>(underscan) / 100;

    ViLayer layer{};
    layer.initialized = true;
    layer.layer_id = g_managedLayerId;

    const Result rc = viSetLayerSize(&layer, w, h);
    if (R_SUCCEEDED(rc)) {
        viSetLayerPosition(&layer, static_cast<float>(kFullW - w) / 2.f,
                           static_cast<float>(kFullH - h) / 2.f);
    }

    DebugLog::log("[menu] underscan=%u%% -> layer %dx%d centered rc=0x%X",
                  underscan, w, h, rc);
    return R_SUCCEEDED(rc);
}
#endif

} // namespace switchu::menu

#ifndef SWITCHU_HOMEBREW
namespace {
constexpr size_t kHeapLadder[] = {
    416u * 1024u * 1024u,
    400u * 1024u * 1024u,
    384u * 1024u * 1024u,
    368u * 1024u * 1024u,
    352u * 1024u * 1024u,
    320u * 1024u * 1024u,
    288u * 1024u * 1024u,
    256u * 1024u * 1024u,
    kMenuAppletHeapSize,
};
}

extern "C" size_t g_switchuHeapSize = 0;

extern "C" void __libnx_initheap(void) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    void* address = nullptr;
    size_t granted = 0;
    for (size_t requested : kHeapLadder) {
        if (R_SUCCEEDED(svcSetHeapSize(&address, requested))) {
            granted = requested;
            break;
        }
    }

    g_switchuHeapSize = granted;
    fake_heap_start = static_cast<char*>(address);
    fake_heap_end = static_cast<char*>(address) + granted;
}
#endif

#ifdef SWITCHU_HOMEBREW
extern "C" void userAppInit(void) {
    timeInitialize();
    plInitialize(PlServiceType_System);
    setInitialize();
    setsysInitialize();
    accountInitialize(AccountServiceType_Application);
    psmInitialize();
    lblInitialize();
    romfsInit();
}

extern "C" void userAppExit(void) {
    lblExit();
    psmExit();
    accountExit();
    setsysExit();
    setExit();
    plExit();
    timeExit();
    romfsExit();
}
#else
extern "C" void __appInit(void) {
    Result rc;

    svcOutputDebugString("[SwitchU-menu] __appInit start", 30);

    rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, 500));

    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, 501));

    rc = appletInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-menu] appletInitialize FAIL", 37);
        diagAbortWithResult(MAKERESULT(Module_Libnx, 502));
    }
    svcOutputDebugString("[SwitchU-menu] appletInitialize OK", 34);

    rc = hidInitialize();
    if (R_FAILED(rc)) svcOutputDebugString("[SwitchU-menu] hidInitialize FAIL", 33);

    timeInitialize();
    __libnx_init_time();
    setsysInitialize();
    setInitialize();
    plInitialize(PlServiceType_System);
    psmInitialize();
    lblInitialize();
    splInitialize();
    accountInitialize(AccountServiceType_System);

    {
        SetSysFirmwareVersion fw = {};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro) | BIT(31));
    }

    rc = fsdevMountSdmc();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-menu] fsdevMountSdmc FAIL, retry", 42);
        svcSleepThread(100'000'000ULL);
        rc = fsdevMountSdmc();
    }

    switchu::FileLog::open("menu");
    DebugLog::openFileLog();
    DebugLog::log("[menu] __appInit complete (sd mount: 0x%X)", rc);

    rc = nsInitialize();
    if (R_FAILED(rc))
        DebugLog::log("[menu] nsInitialize FAILED: 0x%X", rc);
    else
        DebugLog::log("[menu] nsInitialize OK");

    // Single VI/layer probe before libnx's fatal BadGfxInit path.
    // Retrying viInitialize after a failure (e.g. 0xF601) was observed to panic VI
    // itself. Prefer an immediate clean exit; daemon delays at most one relaunch.
    {
        auto failGfxProbe = [](const char* step, Result rc) {
            // Sticky marker so daemon.log can correlate after a hard reboot.
            if (FILE* f = std::fopen("sdmc:/config/SwitchU/last_gfx_fail.txt", "w")) {
                std::fprintf(f, "step=%s rc=0x%X\n", step, rc);
                std::fflush(f);
                std::fclose(f);
                fsdevCommitDevice("sdmc");
            }
            switchu::FileLog::logCommit(
                "[menu] gfx probe %s fail rc=0x%X; clean exit (daemon may delay/skip relaunch)",
                step, rc);
            DebugLog::closeFileLog();
            switchu::FileLog::close();
            svcExitProcess();
        };

        Result lastRc = viInitialize(ViServiceType_Default);
        if (R_FAILED(lastRc))
            failGfxProbe("viInitialize", lastRc);

        ViDisplay display{};
        ViLayer layer{};
        bool gfxReady = false;
        bool haveDisplay = false;
        bool haveLayer = false;
        lastRc = viOpenDefaultDisplay(&display);
        if (R_FAILED(lastRc)) {
            viExit();
            failGfxProbe("viOpenDefaultDisplay", lastRc);
        }
        haveDisplay = true;
        lastRc = viCreateLayer(&display, &layer);
        if (R_SUCCEEDED(lastRc)) {
            haveLayer = true;
            gfxReady = true;
        }

        if (haveLayer)
            viCloseLayer(&layer);
        if (haveDisplay)
            viCloseDisplay(&display);
        viExit();

        if (!gfxReady) {
            // Avoid LibnxError_BadGfxInit (and the vi/omm cascade it caused).
            failGfxProbe("viCreateLayer", lastRc);
        }
    }

    // A library applet's managed layer is left at 1280x720 by libnx, so docked
    // the UI sat unscaled in the middle of the 1080p output. Open the managed
    // layer ourselves (its id is needed) and point libnx's default window at it,
    // then size it to the output, applying the console's TV underscan setting.
    u64 managedLayerId = 0;
    if (R_SUCCEEDED(appletCreateManagedDisplayLayer(&managedLayerId)))
        __nx_vi_layer_id = managedLayerId;

    __nx_win_init();
    DebugLog::log("[menu] __nx_win_init done");

    if (managedLayerId)
        switchu::menu::initScreenFitLayer(managedLayerId);

    svcOutputDebugString("[SwitchU-menu] __appInit done", 29);
}

extern "C" void __appExit(void) {
    DebugLog::closeFileLog();
    switchu::FileLog::close();

    __nx_win_exit();

    nsExit();
    accountExit();
    splExit();
    lblExit();
    psmExit();
    plExit();

    hidExit();
    appletExit();

    setExit();
    setsysExit();
    timeExit();

    fsdevUnmountAll();
    fsExit();
    smExit();
}

static switchu::smi::MenuStartMode readStartMode() {
    LibAppletArgs args{};
    AppletStorage stor{};
    if (R_SUCCEEDED(appletPopInData(&stor))) {
        s64 sz = 0;
        appletStorageGetSize(&stor, &sz);
        if (sz >= (s64)sizeof(LibAppletArgs)) {
            appletStorageRead(&stor, 0, &args, sizeof(args));
        }
        appletStorageClose(&stor);
    }
    return static_cast<switchu::smi::MenuStartMode>(args.LaVersion);
}

static switchu::smi::SystemStatus readSystemStatus() {
    switchu::smi::SystemStatus status{};
    AppletStorage stor{};
    if (R_SUCCEEDED(appletPopInData(&stor))) {
        s64 sz = 0;
        appletStorageGetSize(&stor, &sz);
        if (sz >= (s64)sizeof(switchu::smi::SystemStatus)) {
            appletStorageRead(&stor, 0, &status, sizeof(status));
        }
        appletStorageClose(&stor);
    }
    return status;
}
#endif

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    std::srand(static_cast<unsigned>(std::time(nullptr)));

#ifdef SWITCHU_HOMEBREW
    DebugLog::openFileLog();
    DebugLog::log("[hb] main() entry");
    DebugLog::log("[main] applet config...");
#else
    DebugLog::log("[menu] main() entry");

    auto startMode = readStartMode();
    auto sysStatus = readSystemStatus();

    DebugLog::log("[menu] start mode=%d  suspended=0x%016lX running=%d",
                  (int)startMode, sysStatus.suspended_app_id, sysStatus.app_running);

    {
        std::string sdPath = fmt::format("{}/shaders/", SD_ASSETS);
        nxui::Renderer::setShaderBasePath(sdPath);
        DebugLog::log("[menu] shader path: %s", sdPath.c_str());
    }
#endif

    nxui::GpuDevice::setLogSink([](const char* msg) { DebugLog::log("%s", msg); });

    DebugLog::log("[main] SDL_Init");
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
        DebugLog::log("[main] SDL_Init FAILED: %s", SDL_GetError());

    DebugLog::log("[main] TTF_Init");
    if (TTF_Init() < 0)
        DebugLog::log("[main] TTF_Init FAILED: %s", TTF_GetError());

    DebugLog::log("[main] creating app...");
    {
        nxui::Application app;
#ifdef SWITCHU_HOMEBREW
        auto makeMenuActivity = [](bool fromTutorial = false) -> std::unique_ptr<nxui::Activity> {
            auto activity = std::make_unique<WiiUMenuApp>();
            activity->setTutorialStartupFade(fromTutorial);
            return activity;
        };
        AppConfig startupConfig;
        startupConfig.load();
        if (startupConfig.tutorialCompleted)
            app.setActivity(makeMenuActivity(false));
        else
            app.setActivity(std::make_unique<TutorialActivity>(makeMenuActivity));
        DebugLog::log("[hb] app.initialize...");
        if (app.initialize()) {
            DebugLog::log("[hb] app.run...");
            app.run();
        } else {
            DebugLog::log("[hb] app.initialize FAILED");
        }
        DebugLog::log("[hb] app.shutdown...");
        app.shutdown();
#else
        auto makeMenuActivity = [sysStatus, startMode](bool fromTutorial = false) -> std::unique_ptr<nxui::Activity> {
            auto activity = std::make_unique<WiiUMenuApp>();
            activity->setStartupStatus(sysStatus,
                startMode == switchu::smi::MenuStartMode::AppletReturn);
            activity->setTutorialStartupFade(fromTutorial);
            return activity;
        };
        AppConfig startupConfig;
        startupConfig.load();
        if (startupConfig.tutorialCompleted)
            app.setActivity(makeMenuActivity(false));
        else
            app.setActivity(std::make_unique<TutorialActivity>(makeMenuActivity));
        DebugLog::log("[menu] app.initialize...");
        if (app.initialize()) {
            DebugLog::log("[menu] app.run...");
            app.run();
        } else {
            DebugLog::log("[menu] app.initialize FAILED");
        }
        DebugLog::log("[menu] app.shutdown...");
        app.shutdown();
#endif
    }

    TTF_Quit();
    SDL_Quit();
#ifdef SWITCHU_HOMEBREW
    DebugLog::log("[hb] exit");
    DebugLog::closeFileLog();
#endif
#ifdef SWITCHU_MENU
    DebugLog::log("[menu] exit");
#endif
    return 0;
}
