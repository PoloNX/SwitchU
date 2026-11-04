#include "BluetoothManager.hpp"
#include "core/DebugLog.hpp"
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#endif
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace bluetooth {

#ifdef SWITCHU_MENU

namespace {

constexpr s32 kMaxDiscovered = 15;
constexpr s32 kMaxPaired = 10;

Event g_connectionEvent;

std::vector<BtmAudioDevice> g_pairedDevices;
std::recursive_mutex g_pairedLock;
std::atomic_bool g_pairedChanged = false;

BtmAudioDevice g_connectedDevice = {};
std::recursive_mutex g_connectedLock;
std::atomic_bool g_connectedChanged = false;

std::vector<BtmAudioDevice> g_discoveredDevices;
std::recursive_mutex g_discoveredLock;
std::atomic_bool g_discoveredChanged = false;

std::atomic_bool g_threadRunning = false;
std::atomic_int g_pendingOps = 0;
std::atomic_bool g_discovering = false;
// The search was started by the daemon (btm:sys refused it from this applet).
std::atomic_bool g_discoveringViaDaemon = false;
std::atomic_bool g_initialized = false;

Thread g_thread;
alignas(0x1000) u8 g_threadStack[32 * 1024];

bool UpdateList(std::vector<BtmAudioDevice>& dst, const std::vector<BtmAudioDevice>& src, std::atomic_bool& changed) {
    bool prev = changed;
    if (src.size() != dst.size()) {
        changed = true;
    } else if (!dst.empty() && memcmp(dst.data(), src.data(), sizeof(BtmAudioDevice) * dst.size()) != 0) {
        changed = true;
    }
    dst = src;
    return changed != prev;
}

std::string SanitizedDeviceName(const BtmAudioDevice& device) {
    size_t len = 0;
    while (len < sizeof(device.name) && device.name[len] != '\0')
        ++len;

    std::string name(device.name, device.name + len);
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == 0x7F;
    }), name.end());

    if (!name.empty())
        return name;

    char fallback[32] = {};
    std::snprintf(fallback, sizeof(fallback), "%02X:%02X:%02X:%02X:%02X:%02X",
                  device.addr.address[0], device.addr.address[1], device.addr.address[2],
                  device.addr.address[3], device.addr.address[4], device.addr.address[5]);
    return fallback;
}

void ReloadConnected() {
    std::lock_guard<std::recursive_mutex> lk(g_connectedLock);
    BtdrvAddress prevAddr = g_connectedDevice.addr;
    g_connectedDevice = {};

    s32 count = 0;
    Result rc = btmsysGetConnectedAudioDevices(&g_connectedDevice, 1, &count);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] GetConnectedAudioDevices failed: 0x%x", rc);
        return;
    }
    if (!AddressesEqual(g_connectedDevice.addr, prevAddr)) {
        g_connectedChanged = true;
        if (count > 0)
            DebugLog::log("[bt] Connected: %s", g_connectedDevice.name);
        else
            DebugLog::log("[bt] No connected audio device");
    }
}

void ReloadPaired() {
    std::lock_guard<std::recursive_mutex> lk(g_pairedLock);
    BtmAudioDevice buf[kMaxPaired] = {};
    s32 count = 0;
    Result rc = btmsysGetPairedAudioDevices(buf, kMaxPaired, &count);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] GetPairedAudioDevices failed: 0x%x", rc);
        return;
    }
    std::vector<BtmAudioDevice> list(buf, buf + count);
    if (UpdateList(g_pairedDevices, list, g_pairedChanged))
        DebugLog::log("[bt] Paired devices changed (%d)", count);
}

void ReloadDiscovered() {
    std::lock_guard<std::recursive_mutex> lk(g_discoveredLock);
    BtmAudioDevice buf[kMaxDiscovered] = {};
    s32 count = 0;
    Result rc = btmsysGetDiscoveredAudioDevice(buf, kMaxDiscovered, &count);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] GetDiscoveredAudioDevice failed: 0x%x", rc);
        return;
    }
    std::vector<BtmAudioDevice> list(buf, buf + count);
    if (UpdateList(g_discoveredDevices, list, g_discoveredChanged))
        DebugLog::log("[bt] Discovered devices changed (%d)", count);
}

// btm connect/disconnect/unpair calls can block for seconds (or never return
// while the headset is out of range), so they run on one persistent worker
// instead of the UI thread. The worker is joined, never detached: detach()
// throws when the thread already finished, and that aborted the whole menu.
struct BtOp {
    const char* what;
    std::function<Result()> run;
};
std::thread* g_opThread = nullptr;
std::mutex g_opMutex;
std::condition_variable g_opCv;
std::deque<BtOp> g_ops;
bool g_opStop = false;

void OpWorker() {
    for (;;) {
        BtOp op;
        {
            std::unique_lock<std::mutex> lk(g_opMutex);
            g_opCv.wait(lk, [] { return g_opStop || !g_ops.empty(); });
            if (g_ops.empty())
                return; // stop requested and nothing left to run
            op = std::move(g_ops.front());
            g_ops.pop_front();
        }
        const auto start = std::chrono::steady_clock::now();
        const Result rc = op.run();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        DebugLog::log("[bt] %s finished rc=0x%x in %lld ms", op.what, rc, static_cast<long long>(ms));
        g_connectedChanged = true;
        g_discoveredChanged = true;
        g_pairedChanged = true;
        g_pendingOps--;
    }
}

void RunAsync(const char* what, std::function<Result()> op) {
    std::lock_guard<std::mutex> lk(g_opMutex);
    if (!g_opThread)
        g_opThread = new std::thread(OpWorker);
    g_pendingOps++;
    g_ops.push_back({what, std::move(op)});
    g_opCv.notify_one();
}

void StopOpWorker() {
    std::thread* worker = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_opMutex);
        g_opStop = true;
        g_opCv.notify_all();
        worker = g_opThread;
    }
    if (!worker)
        return;
    // A connect that never returns must not hang shutdown: wait briefly, and
    // if the worker is still busy leave it to die with the process.
    for (int i = 0; i < 60 && g_pendingOps > 0; ++i)
        svcSleepThread(50'000'000);
    if (g_pendingOps == 0) {
        worker->join();
        delete worker;
        std::lock_guard<std::mutex> lk(g_opMutex);
        g_opThread = nullptr;
        g_opStop = false;
    }
}

void ThreadFunc(void*) {
    DebugLog::log("[bt] Thread started");
    Result rc = btmsysAcquireAudioDeviceConnectionEvent(&g_connectionEvent);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] AcquireAudioDeviceConnectionEvent failed: 0x%x", rc);
        return;
    }

    while (g_threadRunning) {
        rc = eventWait(&g_connectionEvent, 500'000'000);
        if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut)) {
            // Only back off on a real error: an unconditional 1 ms sleep here
            // woke this thread 1000 times a second for the whole session, which
            // kept a core busy and defeated console sleep.
            DebugLog::log("[bt] eventWait error: 0x%x", rc);
            svcSleepThread(100'000'000);
            continue;
        }
        if (R_FAILED(rc)) {
            // Timed out. The connection event does not fire for found devices,
            // so a running search is read every half second.
            if (g_discovering)
                ReloadDiscovered();
            continue;
        }
        ReloadConnected();
        ReloadPaired();
        ReloadDiscovered();
        // The service raises the event once per change; a short debounce keeps a
        // burst (pair + connect + profile) from reading the state repeatedly.
        svcSleepThread(50'000'000);
    }

    DebugLog::log("[bt] Thread exiting");
    eventClose(&g_connectionEvent);
}

} // anonymous namespace

void Initialize() {
    if (g_initialized) return;

    Result rc = btmsysInitialize();
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] btmsysInitialize failed: 0x%x", rc);
        return;
    }

    ReloadPaired();
    ReloadConnected();

    g_threadRunning = true;
    rc = threadCreate(&g_thread, ThreadFunc, nullptr, g_threadStack, sizeof(g_threadStack), 0x2C, -2);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] threadCreate failed: 0x%x", rc);
        btmsysExit();
        return;
    }
    rc = threadStart(&g_thread);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] threadStart failed: 0x%x", rc);
        threadClose(&g_thread);
        btmsysExit();
        return;
    }

    g_initialized = true;
    DebugLog::log("[bt] Initialized");
}

void Finalize() {
    if (!g_initialized) return;

    if (g_discovering) StopDiscovery();

    StopOpWorker();
    g_threadRunning = false;
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    btmsysExit();
    g_initialized = false;
    DebugLog::log("[bt] Finalized");
}

bool IsAvailable() { return g_initialized; }

std::vector<BtmAudioDevice> ListPairedAudioDevices() {
    std::lock_guard<std::recursive_mutex> lk(g_pairedLock);
    return g_pairedDevices;
}

bool HasPairedChanges() {
    bool v = g_pairedChanged;
    if (v) g_pairedChanged = false;
    return v;
}

BtmAudioDevice GetConnectedAudioDevice() {
    std::lock_guard<std::recursive_mutex> lk(g_connectedLock);
    return g_connectedDevice;
}

bool HasConnectedChanges() {
    bool v = g_connectedChanged;
    if (v) g_connectedChanged = false;
    return v;
}

std::vector<BtmAudioDevice> ListDiscoveredAudioDevices() {
    std::lock_guard<std::recursive_mutex> lk(g_discoveredLock);
    return g_discoveredDevices;
}

bool HasDiscoveredChanges() {
    bool v = g_discoveredChanged;
    if (v) g_discoveredChanged = false;
    return v;
}

Result ConnectAudioDevice(const BtmAudioDevice& device) {
    g_connectedChanged = true;
    g_discoveredChanged = true;
    g_pairedChanged = true;
    const BtdrvAddress addr = device.addr;
    RunAsync("connect", [addr]() { return btmsysConnectAudioDevice(addr); });
    return 0;
}

Result DisconnectAudioDevice(const BtmAudioDevice& device) {
    {
        std::lock_guard<std::recursive_mutex> lk(g_connectedLock);
        g_connectedDevice = {};
    }
    g_connectedChanged = true;
    const BtdrvAddress addr = device.addr;
    RunAsync("disconnect", [addr]() { return btmsysDisconnectAudioDevice(addr); });
    return 0;
}

Result UnpairAudioDevice(const BtmAudioDevice& device) {
    g_pairedChanged = true;
    g_discoveredChanged = true;
    const BtdrvAddress addr = device.addr;
    RunAsync("unpair", [addr]() { return btmsysRemoveAudioDevicePairing(addr); });
    return 0;
}

std::string DeviceName(const BtmAudioDevice& device) {
    return SanitizedDeviceName(device);
}

void StartDiscovery() {
    auto markStarted = [](const char* how) {
        g_discovering = true;
        g_discoveredChanged = true;
        DebugLog::log("[bt] Discovery started (%s)", how);
    };

    bool alreadySearching = false;
    if (R_SUCCEEDED(btmsysIsDiscoveryingAudioDevice(&alreadySearching)) && alreadySearching) {
        markStarted("already running");
        return;
    }
    bool radioOn = false;
    const Result radioRc = btmsysGetRadioOnOff(&radioOn);
    bool gamepadPairing = false;
    const Result pairingRc = btmsysIsGamepadPairingStarted(&gamepadPairing);
    if (R_SUCCEEDED(pairingRc) && gamepadPairing) {
        // A controller pairing session left running holds the radio's search.
        const Result cancelRc = btmsysCancelGamepadPairing();
        DebugLog::log("[bt] Gamepad pairing was running: cancel rc=0x%x", cancelRc);
    }

    const Result rc = btmsysStartAudioDeviceDiscovery();
    if (R_SUCCEEDED(rc)) {
        g_discoveringViaDaemon = false;
        markStarted("menu");
        return;
    }
    DebugLog::log("[bt] StartDiscovery failed: 0x%x radio=%d(rc=0x%x) gamepadPairing=%d(rc=0x%x)",
                  rc, radioOn ? 1 : 0, radioRc, gamepadPairing ? 1 : 0, pairingRc);

    // Stock HOME searches from the system applet (System Settings lives in
    // qlaunch); from this library applet btm:sys answers 0x4028F, as uLaunch's
    // menu also sees. Ask the daemon, which holds the system applet.
    const Result daemonRc = switchu::menu::smi_cmd::setAudioDeviceDiscovery(true);
    if (R_SUCCEEDED(daemonRc)) {
        g_discoveringViaDaemon = true;
        markStarted("daemon");
        return;
    }
    DebugLog::log("[bt] StartDiscovery via daemon failed: 0x%x", daemonRc);
}

void StopDiscovery() {
    bool viaDaemon = g_discoveringViaDaemon;
    Result rc = viaDaemon
        ? switchu::menu::smi_cmd::setAudioDeviceDiscovery(false)
        : btmsysStopAudioDeviceDiscovery();
    if (R_FAILED(rc) && !viaDaemon) {
        // A search found already running may be the daemon's (menu restarted).
        viaDaemon = true;
        rc = switchu::menu::smi_cmd::setAudioDeviceDiscovery(false);
    }
    if (R_FAILED(rc))
        DebugLog::log("[bt] StopDiscovery (%s) failed: 0x%x", viaDaemon ? "daemon" : "menu", rc);
    else
        DebugLog::log("[bt] Discovery stopped (%s)", viaDaemon ? "daemon" : "menu");
    g_discovering = false;
    g_discoveringViaDaemon = false;
    g_discoveredChanged = true;
}

bool IsDiscovering() { return g_discovering; }

#else

void Initialize() {}
void Finalize() {}
bool IsAvailable() { return false; }
std::vector<BtmAudioDevice> ListPairedAudioDevices() { return {}; }
bool HasPairedChanges() { return false; }
BtmAudioDevice GetConnectedAudioDevice() { return {}; }
bool HasConnectedChanges() { return false; }
std::vector<BtmAudioDevice> ListDiscoveredAudioDevices() { return {}; }
bool HasDiscoveredChanges() { return false; }
Result ConnectAudioDevice(const BtmAudioDevice&) { return 0; }
Result DisconnectAudioDevice(const BtmAudioDevice&) { return 0; }
Result UnpairAudioDevice(const BtmAudioDevice&) { return 0; }
std::string DeviceName(const BtmAudioDevice&) { return {}; }
void StartDiscovery() {}
void StopDiscovery() {}
bool IsDiscovering() { return false; }

#endif

}
