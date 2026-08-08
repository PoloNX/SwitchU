#include "ecs.hpp"
#include <switchu/smi_protocol.hpp>
#include <switchu/file_log.hpp>

#include <stratosphere.hpp>
#include <stratosphere/fssrv/interface_adapters/fssrv_filesystem_interface_adapter.hpp>

#include <atomic>
#include <memory>
#include <mutex>

namespace ams::os {
    void Initialize();
}

namespace switchu::daemon {

namespace {

struct EcsServerOptions {
    static constexpr size_t PointerBufferSize = 0x800;
    static constexpr size_t MaxDomains = 0x40;
    static constexpr size_t MaxDomainObjects = 0x100;
    static constexpr bool CanDeferInvokeRequest = false;
    static constexpr bool CanManageMitmServers = false;
};

using EcsServerManager = ::ams::sf::hipc::ServerManager<0, EcsServerOptions, 8>;

constexpr size_t kAmsHeapSize = 2 * 1024 * 1024;
constexpr size_t kManagerThreadStackSize = 0x8000;
constexpr s32 kManagerThreadPriority = 10;

alignas(::ams::os::MemoryPageSize) uint8_t g_amsHeap[kAmsHeapSize] = {};
alignas(::ams::os::ThreadStackAlignment) uint8_t g_managerThreadStack[kManagerThreadStackSize] = {};

EcsServerManager g_manager;
::ams::os::ThreadType g_managerThread = {};
std::once_flag g_runtimeOnce;
std::atomic<bool> g_allocatorReady{false};
std::atomic<bool> g_runtimeReady{false};

void managerThreadEntry(void*) {
    switchu::FileLog::log("[ecs] stratosphere manager thread started");
    g_manager.LoopProcess();
    switchu::FileLog::log("[ecs] stratosphere manager thread exiting");
}

Result initializeRuntime() {
    Result rc = 0;
    std::call_once(g_runtimeOnce, [&] {
        initializeExternalContentAllocator();

        auto amsRc = ::ams::os::CreateThread(&g_managerThread, managerThreadEntry, nullptr,
                                             g_managerThreadStack, sizeof(g_managerThreadStack),
                                             kManagerThreadPriority);
        rc = amsRc.GetValue();
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[ecs] stratosphere CreateThread FAIL: 0x%X", rc);
            return;
        }

        ::ams::os::StartThread(&g_managerThread);
        g_runtimeReady.store(true);
        switchu::FileLog::log("[ecs] stratosphere runtime ready");
    });

    return g_runtimeReady.load() ? 0 : rc;
}

Result ldrAtmosRegisterExternalCode(uint64_t program_id, Handle* out_h) {
    return serviceDispatchIn(ldrShellGetServiceSession(),
        smi::kLdrAtmosRegisterExternalCode, program_id,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = out_h,
    );
}

Result ldrAtmosUnregisterExternalCode(uint64_t program_id) {
    return serviceDispatchIn(ldrShellGetServiceSession(),
        smi::kLdrAtmosUnregisterExternalCode, program_id);
}

}

void initializeExternalContentAllocator() {
    bool expected = false;
    if (g_allocatorReady.compare_exchange_strong(expected, true)) {
        ::ams::init::InitializeAllocator(g_amsHeap, sizeof(g_amsHeap));
        ::ams::os::Initialize();
        ::ams::os::SetThreadNamePointer(::ams::os::GetCurrentThread(), "SwitchU.daemon.Main");
    }
}

Result registerExternalContent(uint64_t program_id, const char* exefs_path) {
    Result rc = initializeRuntime();
    if (R_FAILED(rc))
        return rc;

    Handle move_h = INVALID_HANDLE;
    rc = ldrAtmosRegisterExternalCode(program_id, &move_h);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[ecs] RegisterExternalCode FAIL: 0x%X", rc);
        return rc;
    }
    switchu::FileLog::log("[ecs] RegisterExternalCode ok program=0x%016lX server=0x%X",
                          program_id, move_h);

    FsFileSystem sd_fs = {};
    rc = fsOpenSdCardFileSystem(&sd_fs);
    if (R_FAILED(rc)) {
        ldrAtmosUnregisterExternalCode(program_id);
        svcCloseHandle(move_h);
        switchu::FileLog::log("[ecs] fsOpenSdCard FAIL: 0x%X", rc);
        return rc;
    }

    auto remote_sd_fs = std::make_shared<::ams::fs::RemoteFileSystem>(sd_fs);
    auto subdir_fs = std::make_shared<::ams::fssystem::SubDirectoryFileSystem>(std::move(remote_sd_fs));

    ::ams::fs::Path exefs_fs_path;
    auto amsRc = exefs_fs_path.Initialize(exefs_path, std::strlen(exefs_path));
    if (amsRc.IsSuccess())
        amsRc = exefs_fs_path.Normalize(::ams::fs::PathFlags{});
    if (amsRc.IsSuccess())
        amsRc = subdir_fs->Initialize(exefs_fs_path);

    if (amsRc.IsFailure()) {
        rc = amsRc.GetValue();
        ldrAtmosUnregisterExternalCode(program_id);
        svcCloseHandle(move_h);
        switchu::FileLog::log("[ecs] SubDirectoryFileSystem init FAIL: 0x%X path=%s", rc, exefs_path);
        return rc;
    }

    auto fs_object = ::ams::sf::CreateSharedObjectEmplaced<
        ::ams::fssrv::sf::IFileSystem,
        ::ams::fssrv::impl::FileSystemInterfaceAdapter>(std::move(subdir_fs), false);

    amsRc = g_manager.RegisterSession(move_h, ::ams::sf::cmif::ServiceObjectHolder(std::move(fs_object)));
    if (amsRc.IsFailure()) {
        rc = amsRc.GetValue();
        ldrAtmosUnregisterExternalCode(program_id);
        svcCloseHandle(move_h);
        switchu::FileLog::log("[ecs] RegisterSession FAIL: 0x%X", rc);
        return rc;
    }

    switchu::FileLog::log("[ecs] registered %s -> 0x%016lX via stratosphere", exefs_path, program_id);
    return 0;
}

Result unregisterExternalContent(uint64_t program_id) {
    Result rc = ldrAtmosUnregisterExternalCode(program_id);
    if (R_FAILED(rc))
        switchu::FileLog::log("[ecs] Unregister FAIL: 0x%X", rc);
    else
        switchu::FileLog::log("[ecs] unregistered 0x%016lX", program_id);
    return rc;
}

}
