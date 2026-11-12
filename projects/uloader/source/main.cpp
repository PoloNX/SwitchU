#include <ul/loader/loader_SelfProcess.hpp>
#include <ul/loader/loader_Target.hpp>
#include <ul/loader/loader_Input.hpp>
#include <ul/loader/loader_ProgramIdUtils.hpp>
#include <ul/loader/loader_Common.hpp>

#include <memory>

namespace {

    constexpr auto HbloaderSettingsSectionName = "hbloader";

    template <typename T>
    inline Result GetHbloaderSetting(const char *key, T &out_value) {
        u64 setting_size = 0;
        const Result rc = setsysGetSettingsItemValue(
            HbloaderSettingsSectionName, key, std::addressof(out_value),
            sizeof(out_value), &setting_size);
        if (R_FAILED(rc)) {
            return rc;
        }
        if (setting_size != sizeof(out_value)) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        return ul::loader::ResultSuccess;
    }

    // uLoader only needs enough heap for libnx and the small amount of loader
    // bookkeeping it performs; the launched NRO gets the process heap.
    constexpr size_t HeapSize = 64 * 1024;
    u8 g_Heap[HeapSize] = {};

}

extern "C" {

    u32 __nx_applet_exit_mode = 2;

    u32 __nx_fs_num_sessions = 1;
    u32 __nx_fsdev_direntry_cache_size = 1;
    bool __nx_fsdev_support_cwd = false;

    extern u8 *fake_heap_start;
    extern u8 *fake_heap_end;

    void __libnx_initheap() {
        fake_heap_start = g_Heap;
        fake_heap_end = g_Heap + HeapSize;
    }

    void __appInit() {}
    void __appExit() {}

}

int main() {
    UL_RC_ASSERT(smInitialize());

    UL_RC_ASSERT(fsInitialize());
    UL_RC_ASSERT(fsdevMountSdmc());

    ul::loader::Log("[uloader] starting");

    UL_RC_ASSERT(setsysInitialize());

    SetSysFirmwareVersion fw_ver;
    UL_RC_ASSERT(setsysGetFirmwareVersion(&fw_ver));
    // Atmosphère is always present (it launched us); set the version with its
    // presence bit so libnx follows the Atmosphère code paths.
    hosversionSet(MAKEHOSVERSION(fw_ver.major, fw_ver.minor, fw_ver.micro) | BIT(31));

    // hbloader settings are optional. Missing values simply mean "use the full
    // process heap", which is the right behaviour for the application variant.
    u64 applet_heap_size = 0;
    if (R_FAILED(GetHbloaderSetting("applet_heap_size", applet_heap_size))) {
        applet_heap_size = 0;
    }
    u64 applet_heap_reservation_size = 0;
    if (R_FAILED(GetHbloaderSetting("applet_heap_reservation_size", applet_heap_reservation_size))) {
        applet_heap_reservation_size = 0;
    }

    setsysExit();

    u64 self_program_id;
    UL_RC_ASSERT(ul::loader::GetSelfProgramId(self_program_id));
    ul::loader::DetermineSelfAppletType(self_program_id);

    ul::loader::TargetInput target_ipt;
    UL_RC_ASSERT(ul::loader::ReadTargetInput(target_ipt));

    ul::loader::Log("[uloader] targeting '%s' with argv '%s' (once: %d)",
                    target_ipt.nro_path, target_ipt.nro_argv,
                    target_ipt.target_once ? 1 : 0);

    // Capture the heap plan while the SD card is still mounted. After
    // fsdevUnmountAll()/fsExit() the log file is unreachable, so a failure in
    // SetupTargetHeap would otherwise leave no trace.
    {
        u64 total_mem = 0;
        u64 used_mem = 0;
        svcGetInfo(&total_mem, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used_mem, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        ul::loader::Log(
            "[uloader] heap plan total=0x%lX used=0x%lX auto_rec=%d "
            "applet_heap=0x%lX applet_reserv=0x%lX",
            static_cast<unsigned long>(total_mem),
            static_cast<unsigned long>(used_mem),
            target_ipt.is_auto_game_recording ? 1 : 0,
            static_cast<unsigned long>(applet_heap_size),
            static_cast<unsigned long>(applet_heap_reservation_size));
    }

    fsdevUnmountAll();
    fsExit();
    smExit();

    ul::loader::Target(target_ipt, applet_heap_size, applet_heap_reservation_size);
}
