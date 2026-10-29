/*
 * SwitchU-MTP — optional Atmosphère sysmodule (GPLv2 via Atmosphère haze).
 *
 * Not linked into SwitchU-menu. Install under atmosphere/contents/ only if you
 * want USB MTP. The menu signals intent with a flag file:
 *
 *   sdmc:/config/SwitchU/mtp_enable   present  → start haze MTP
 *                                   absent   → stop and release USB
 *
 * So MTP is active while HOME is up (menu creates the flag) and releases USB
 * when the menu exits (games can reclaim the bus).
 */

#include <haze.hpp>

#include <atomic>
#include <cstdio>
#include <switch.h>

namespace {

constexpr const char* kEnableFlag = "sdmc:/config/SwitchU/mtp_enable";

class StopConsumer : public haze::EventConsumer {
public:
    haze::EventReactor* reactor = nullptr;
    void ProcessEvent() override {
        if (reactor)
            reactor->SetResult(haze::ResultStopRequested());
    }
};

haze::PtpObjectHeap g_heap;
haze::EventReactor g_reactor;
haze::PtpResponder g_responder;
StopConsumer g_stop;
UEvent g_stop_event{};
Thread g_thread{};
std::atomic<bool> g_threadLive{false};
std::atomic<bool> g_serving{false};

bool flagPresent() {
    FILE* f = fopen(kEnableFlag, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

void mtpThread(void*) {
    if (R_FAILED(haze::LoadDeviceProperties()))
        return;

    if (R_FAILED(g_responder.Initialize(&g_reactor, &g_heap))) {
        g_responder.Finalize();
        return;
    }

    g_stop.reactor = &g_reactor;
    if (g_reactor.AddConsumer(&g_stop, waiterForUEvent(&g_stop_event))) {
        g_serving.store(true, std::memory_order_release);
        (void)g_responder.LoopProcess();
        g_serving.store(false, std::memory_order_release);
        g_reactor.RemoveConsumer(&g_stop);
    }
    g_responder.Finalize();
}

void ensureStarted() {
    if (g_threadLive.load(std::memory_order_acquire))
        return;

    ueventCreate(&g_stop_event, false);
    g_reactor.SetResult(haze::ResultSuccess());

    if (R_FAILED(threadCreate(&g_thread, mtpThread, nullptr, nullptr, 0x10000, 0x2E, 2)) &&
        R_FAILED(threadCreate(&g_thread, mtpThread, nullptr, nullptr, 0x10000, 0x2E, -2))) {
        return;
    }

    threadStart(&g_thread);
    g_threadLive.store(true, std::memory_order_release);
}

void ensureStopped() {
    if (!g_threadLive.load(std::memory_order_acquire))
        return;
    ueventSignal(&g_stop_event);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_threadLive.store(false, std::memory_order_release);
    g_serving.store(false, std::memory_order_release);
}

} // namespace

extern "C" void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);
}

extern "C" void __appExit(void) {
    ensureStopped();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Wait for SD; early boot may race the mount.
    for (int i = 0; i < 50 && !flagPresent() && !g_threadLive; ++i)
        svcSleepThread(100'000'000ULL); // 100 ms

    while (true) {
        const bool want = flagPresent();
        if (want)
            ensureStarted();
        else
            ensureStopped();
        svcSleepThread(250'000'000ULL); // 250 ms
    }
}
