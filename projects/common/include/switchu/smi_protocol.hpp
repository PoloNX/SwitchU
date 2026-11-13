#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace switchu::smi {

static constexpr uint32_t kCommandMagic      = 0x53575543;
static constexpr uint32_t kStorageSize       = 0x8000;
static constexpr uint32_t kMaxRetries        = 5000;
static constexpr uint64_t kRetrySleepNs      = 10'000'000;
enum class MenuMessage : uint32_t {
    Invalid               =  0,
    HomeRequest           =  1,
    ApplicationExited     =  2,
    ApplicationSuspended  =  3,
    SdCardEjected         =  4,
    AppRecordsChanged     =  5,
    SleepSequence         =  6,
    WakeUp                =  7,
    GameCardMountFailure  =  8,
    AppViewFlagsUpdate    =  9,
    BatteryStatusChanged  = 10,
    OperationFailed       = 11,
    ShowLock              = 12, // power-button Lock mode: show Lock, stay awake
    // Daemon saw AppletMessage_OperationModeChanged (dock/TV ↔ handheld).
    // payload = AppletOperationMode (0 handheld, 1 console).
    OperationModeChanged  = 13,
};

enum class SystemMessage : uint32_t {
    Invalid               =  0,

    LaunchApplication     =  1,
    ResumeApplication     =  2,
    TerminateApplication  =  3,

    LaunchAlbum           = 10,
    LaunchMiiEditor       = 11,
    LaunchControllers     = 12,
    LaunchNetConnect      = 13,
    LaunchUserPage        = 14,
    LaunchControllerRemapping = 15,
    LaunchUserCreator     = 16,
    // Full-RAM .nro launch: uLoader runs over an installed donor application.
    LaunchHomebrew        = 17,

    EnterSleep            = 20,
    Shutdown              = 21,
    Reboot                = 22,
    RequestForeground     = 23,

    GetAppList            = 30,
    GetSystemStatus       = 31,
    IsApplicationValid    = 32,
    SetManualDateTime     = 33,
    SetInternetTimeSync   = 34,
    SetLockScreen         = 35,
    // Bluetooth audio device search run by the daemon (the system applet):
    // btm:sys refuses it from the menu's library applet with 0x4028F.
    StartAudioDiscovery   = 36,
    StopAudioDiscovery    = 37,
    // POSIX UTC seconds — skip timezone calendar convert (sysmodule coexistence).
    SetPosixNetworkTime   = 38,

    MenuReady             = 40,
    MenuClosing           = 41,
};

enum class MenuStartMode : uint32_t {
    MainMenu       = 0,
    StartupBoot    = 1,
    AppletReturn   = 2,
    LockScreen     = 3,   // wake with suspended game: show Lock view first
};

// power_button: 0=Sleep (stock), 1=Lock only, 2=Lock then Sleep
struct LockScreenArgs {
    uint8_t enabled;
    uint8_t power_button;
    uint8_t _pad[6];
};
static_assert(sizeof(LockScreenArgs) == 8);

struct CommandHeader {
    uint32_t magic;
    uint32_t message;
    uint64_t request_id;
};
static_assert(sizeof(CommandHeader) == 16);

struct LaunchAppArgs {
    uint64_t title_id;
    uint8_t  user_uid[16];
};
static_assert(sizeof(LaunchAppArgs) == 24);

// uLoader's target description, read from the launch parameter storage.
// Keep this layout byte-for-byte identical to projects/uloader's reader.
static constexpr uint32_t kTargetInputMagic = 0x49444C55; // "ULDI"
static constexpr uint32_t kNroPathSize      = 512;
static constexpr uint32_t kNroArgvSize      = 2048;
static constexpr uint32_t kMenuCaptionSize  = 1024;

struct TargetInput {
    static constexpr uint32_t Magic = kTargetInputMagic;

    uint32_t magic = kTargetInputMagic;
    uint8_t  target_once = 0;
    uint8_t  is_auto_game_recording = 0;
    uint8_t  _pad[2] = {};
    char     nro_path[kNroPathSize] = {};
    char     nro_argv[kNroArgvSize] = {};
    char     menu_caption[kMenuCaptionSize] = {};
};
static_assert(offsetof(TargetInput, magic) == 0);
static_assert(offsetof(TargetInput, target_once) == 4);
static_assert(offsetof(TargetInput, is_auto_game_recording) == 5);
static_assert(offsetof(TargetInput, nro_path) == 8);
static_assert(offsetof(TargetInput, nro_argv) == 520);
static_assert(offsetof(TargetInput, menu_caption) == 2568);
static_assert(sizeof(TargetInput) == 3592);

// Homebrew is launched by registering uLoader as external content for an
// installed application title, creating that application, and pushing this
// storage through AppletLaunchParameterKind_UserChannel.
struct LaunchHomebrewArgs {
    TargetInput target;
    uint64_t    donor_title_id;
    // Synthetic menu title ID (0xFB... | homebrew id) so the menu can highlight
    // the launched homebrew instead of the donor it is running over. 0 means
    // "unknown; keep reporting the donor".
    uint64_t    homebrew_title_id;
    uint8_t     user_uid[16];
};
static_assert(sizeof(LaunchHomebrewArgs) == 3624);

// Program IDs from here up are full applications, which appletCreateApplication
// accepts. Applet-range titles (0100000000001000-0100000000001FFF) cannot be
// used as the donor for uLoader's application variant.
static constexpr uint64_t kApplicationTitleIdMin = 0x0100000000010000ULL;
static constexpr uint64_t kApplicationTitleIdMax = 0x01FFFFFFFFFFFFFFULL;

struct UserArgs {
    uint8_t user_uid[16];
};
static_assert(sizeof(UserArgs) == 16);

struct ManualDateTimeArgs {
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
};
static_assert(sizeof(ManualDateTimeArgs) == 20);

struct PosixTimeArgs {
    uint64_t posix_utc;
};
static_assert(sizeof(PosixTimeArgs) == 8);

struct InternetTimeSyncArgs {
    uint8_t enabled;
    uint8_t _pad[7];
};
static_assert(sizeof(InternetTimeSyncArgs) == 8);

struct OperationOutcome {
    uint64_t request_id;
    uint64_t title_id;
    uint32_t command;
    uint32_t result;
};
static_assert(sizeof(OperationOutcome) == 24);

struct SystemStatus {
    uint64_t  suspended_app_id;
    uint8_t   selected_user[16];
    bool      app_running;
    // Daemon-lived: public clock already applied this console boot.
    uint8_t   web_clock_synced;
    uint8_t   _pad[6];
    OperationOutcome last_failure;
};
static_assert(sizeof(SystemStatus) == 56);

struct AppEntryHeader {
    uint64_t  title_id;
    uint32_t  name_len;
    uint32_t  icon_data_len;
    uint32_t  view_flags;
    uint8_t   startup_user_account;
    uint8_t   startup_user_account_option;
    uint8_t   startup_user_known;
    uint8_t   _pad;
};
static_assert(sizeof(AppEntryHeader) == 24);

static constexpr uint32_t kNotifyMagic = 0x53574E54;

struct DaemonNotification {
    uint32_t magic;
    MenuMessage msg;
    uint64_t  app_id;
    uint32_t  payload;
    uint32_t  _pad;
};

static constexpr uint32_t kBatteryPercentMask  = 0xFF;
static constexpr uint32_t kBatteryChargerShift = 8;
static constexpr uint32_t kBatteryChargerMask  = 0xFF << kBatteryChargerShift;

inline uint32_t makeBatteryPayload(uint32_t percentage, uint32_t chargerType) {
    if (percentage > 100)
        percentage = 100;
    return (percentage & kBatteryPercentMask)
        | ((chargerType & 0xFF) << kBatteryChargerShift);
}

inline uint32_t batteryPayloadPercentage(uint32_t payload) {
    return payload & kBatteryPercentMask;
}

inline uint32_t batteryPayloadChargerType(uint32_t payload) {
    return (payload & kBatteryChargerMask) >> kBatteryChargerShift;
}

inline bool batteryPayloadCharging(uint32_t payload) {
    return batteryPayloadChargerType(payload) != 0;
}

// App catalog file the daemon publishes and the menu reads at startup.
// A fresh catalog is staged at kAppCatalogTmpPath and swapped in by rename;
// the previous copy is kept at kAppCatalogBakPath for the duration of the
// swap so a reader landing in that window still finds a valid catalog.
static constexpr const char* kAppCatalogPath    = "sdmc:/config/SwitchU/applist.bin";
static constexpr const char* kAppCatalogTmpPath = "sdmc:/config/SwitchU/applist.tmp";
static constexpr const char* kAppCatalogBakPath = "sdmc:/config/SwitchU/applist.bak";

static constexpr uint64_t kMenuTakeoverProgramId = 0x010000000000100DULL;
static constexpr uint64_t kMenuProcessProgramId  = 0x010000000000FFFFULL;
static constexpr uint32_t kLdrAtmosRegisterExternalCode   = 65000;
static constexpr uint32_t kLdrAtmosUnregisterExternalCode = 65001;

}
