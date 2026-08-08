#pragma once
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

    EnterSleep            = 20,
    Shutdown              = 21,
    Reboot                = 22,
    RequestForeground     = 23,

    GetAppList            = 30,
    GetSystemStatus       = 31,
    IsApplicationValid    = 32,

    MenuReady             = 40,
    MenuClosing           = 41,
};

enum class MenuStartMode : uint32_t {
    MainMenu       = 0,
    StartupBoot    = 1,
};

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

struct UserArgs {
    uint8_t user_uid[16];
};
static_assert(sizeof(UserArgs) == 16);

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
    uint8_t   _pad[7];
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

static constexpr uint64_t kMenuTakeoverProgramId = 0x010000000000100DULL;
static constexpr uint64_t kMenuProcessProgramId  = 0x010000000000FFFFULL;
static constexpr uint32_t kLdrAtmosRegisterExternalCode   = 65000;
static constexpr uint32_t kLdrAtmosUnregisterExternalCode = 65001;

}
