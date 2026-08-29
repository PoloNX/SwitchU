#pragma once
#include <switchu/smi_protocol.hpp>
#include <switchu/smi_helpers.hpp>
#include <switch.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <atomic>

namespace switchu::menu::smi_cmd {

inline uint64_t nextRequestId() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

static Result pushOutStorage(const void* data, size_t size) {
    AppletStorage stor{};
    Result rc = appletCreateStorage(&stor, static_cast<s64>(size));
    if (R_FAILED(rc)) return rc;

    rc = appletStorageWrite(&stor, 0, data, size);
    if (R_FAILED(rc)) { appletStorageClose(&stor); return rc; }

    rc = appletPushInteractiveOutData(&stor);
    appletStorageClose(&stor);
    return rc;
}

static Result popInStorage(void* out, size_t maxSize, size_t* outActual) {
    AppletStorage stor{};
    Result rc = appletPopInteractiveInData(&stor);
    if (R_FAILED(rc)) return rc;

    s64 sz = 0;
    appletStorageGetSize(&stor, &sz);
    size_t toRead = (size_t)sz < maxSize ? (size_t)sz : maxSize;
    rc = appletStorageRead(&stor, 0, out, toRead);
    appletStorageClose(&stor);
    if (outActual) *outActual = toRead;
    return rc;
}

inline Result sendSimple(smi::SystemMessage msg) {
    smi::CommandHeader hdr{smi::kCommandMagic, static_cast<uint32_t>(msg), nextRequestId()};
    return pushOutStorage(&hdr, sizeof(hdr));
}

inline Result launchApplication(uint64_t titleId, AccountUid uid) {
    uint8_t buf[sizeof(smi::CommandHeader) + sizeof(smi::LaunchAppArgs)]{};
    auto* hdr = reinterpret_cast<smi::CommandHeader*>(buf);
    auto* args = reinterpret_cast<smi::LaunchAppArgs*>(buf + sizeof(smi::CommandHeader));

    hdr->magic    = smi::kCommandMagic;
    hdr->message  = static_cast<uint32_t>(smi::SystemMessage::LaunchApplication);
    hdr->request_id = nextRequestId();
    args->title_id = titleId;
    std::memcpy(args->user_uid, &uid, sizeof(uid));

    return pushOutStorage(buf, sizeof(buf));
}

inline Result launchUserPage(AccountUid uid) {
    uint8_t buf[sizeof(smi::CommandHeader) + sizeof(smi::UserArgs)]{};
    auto* hdr = reinterpret_cast<smi::CommandHeader*>(buf);
    auto* args = reinterpret_cast<smi::UserArgs*>(buf + sizeof(smi::CommandHeader));

    hdr->magic    = smi::kCommandMagic;
    hdr->message  = static_cast<uint32_t>(smi::SystemMessage::LaunchUserPage);
    hdr->request_id = nextRequestId();
    std::memcpy(args->user_uid, &uid, sizeof(uid));

    return pushOutStorage(buf, sizeof(buf));
}

inline Result setManualDateTime(const smi::ManualDateTimeArgs& value) {
    uint8_t buf[sizeof(smi::CommandHeader) + sizeof(smi::ManualDateTimeArgs)]{};
    auto* hdr = reinterpret_cast<smi::CommandHeader*>(buf);
    auto* args = reinterpret_cast<smi::ManualDateTimeArgs*>(buf + sizeof(smi::CommandHeader));

    hdr->magic = smi::kCommandMagic;
    hdr->message = static_cast<uint32_t>(smi::SystemMessage::SetManualDateTime);
    const uint64_t requestId = nextRequestId();
    hdr->request_id = requestId;
    *args = value;

    Result rc = pushOutStorage(buf, sizeof(buf));
    if (R_FAILED(rc)) return rc;

    uint8_t response[smi::kStorageSize]{};
    size_t actual = 0;
    for (int retry = 0; retry < 200; retry++) {
        rc = popInStorage(response, sizeof(response), &actual);
        if (R_SUCCEEDED(rc)) {
            if (actual >= sizeof(smi::CommandHeader)) {
                smi::CommandHeader responseHeader{};
                std::memcpy(&responseHeader, response, sizeof(responseHeader));
                if (responseHeader.magic == smi::kCommandMagic &&
                    responseHeader.request_id == requestId) {
                    return static_cast<Result>(responseHeader.message);
                }
            }
        }
        svcSleepThread(10'000'000ULL);
    }
    if (R_FAILED(rc)) return rc;
    return MAKERESULT(Module_Libnx, 0xFF);
}

inline Result resumeApplication() {
    return sendSimple(smi::SystemMessage::ResumeApplication);
}

inline Result terminateApplication() {
    return sendSimple(smi::SystemMessage::TerminateApplication);
}

inline Result enterSleep()  { return sendSimple(smi::SystemMessage::EnterSleep); }
inline Result shutdown()    { return sendSimple(smi::SystemMessage::Shutdown); }
inline Result reboot()      { return sendSimple(smi::SystemMessage::Reboot); }
inline Result menuReady()      { return sendSimple(smi::SystemMessage::MenuReady); }
inline Result menuClosing()    { return sendSimple(smi::SystemMessage::MenuClosing); }

struct AppEntry {
    uint64_t titleId;
    uint32_t nameLen;
    uint32_t iconLen;
    uint32_t viewFlags = 0;
    bool startupUserKnown = false;
    uint8_t startupUserAccount = 1;
    uint8_t startupUserAccountOption = 0;
    std::string name;
    std::vector<uint8_t> icon;
};

inline Result getAppList(std::vector<AppEntry>& outList, bool waitForDaemon = true) {
    constexpr uint32_t kMaxCatalogEntries = 2048;
    constexpr uint32_t kMaxTitleNameBytes = 0x200;
    constexpr uint32_t kMaxIconBytes = 0x40000;
    constexpr Result kMalformedCatalog = MAKERESULT(Module_Libnx, 0xFD);

    outList.clear();
    std::ifstream file;
    const int retries = waitForDaemon ? 20 : 1;
    for (int retry = 0; retry < retries && !file.is_open(); ++retry) {
        file.open("sdmc:/config/SwitchU/applist.bin", std::ios::binary);
        if (!file.is_open() && waitForDaemon) svcSleepThread(50'000'000ULL);
    }
    if (!file.is_open()) return MAKERESULT(Module_Libnx, 0xFE);

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fileSize < static_cast<std::streamoff>(sizeof(uint32_t)))
        return kMalformedCatalog;

    uint32_t count = 0;
    if (!file.read(reinterpret_cast<char*>(&count), sizeof(count)) ||
        count > kMaxCatalogEntries)
        return kMalformedCatalog;

    outList.reserve(count);
    std::uint64_t consumed = sizeof(count);

    for (uint32_t i = 0; i < count; ++i) {
        smi::AppEntryHeader eh{};
        if (!file.read(reinterpret_cast<char*>(&eh), sizeof(eh))) {
            outList.clear();
            return kMalformedCatalog;
        }
        consumed += sizeof(eh);
        const std::uint64_t payloadSize =
            static_cast<std::uint64_t>(eh.name_len) + eh.icon_data_len;
        if (eh.name_len > kMaxTitleNameBytes ||
            eh.icon_data_len > kMaxIconBytes ||
            consumed + payloadSize > static_cast<std::uint64_t>(fileSize)) {
            outList.clear();
            return kMalformedCatalog;
        }

        AppEntry ent{};
        ent.titleId = eh.title_id;
        ent.nameLen = eh.name_len;
        ent.iconLen = eh.icon_data_len;
        ent.viewFlags = eh.view_flags;
        ent.startupUserKnown = eh.startup_user_known != 0;
        ent.startupUserAccount = eh.startup_user_account;
        ent.startupUserAccountOption = eh.startup_user_account_option;

        if (eh.name_len > 0) {
            ent.name.resize(eh.name_len);
            if (!file.read(ent.name.data(), static_cast<std::streamsize>(eh.name_len))) {
                outList.clear();
                return kMalformedCatalog;
            }
        }

        if (eh.icon_data_len > 0) {
            ent.icon.resize(eh.icon_data_len);
            if (!file.read(reinterpret_cast<char*>(ent.icon.data()), static_cast<std::streamsize>(eh.icon_data_len))) {
                outList.clear();
                return kMalformedCatalog;
            }
        }

        consumed += payloadSize;
        outList.push_back(std::move(ent));
    }

    return 0;
}


inline Result getSystemStatus(smi::SystemStatus& out) {
    Result rc = sendSimple(smi::SystemMessage::GetSystemStatus);
    if (R_FAILED(rc)) return rc;

    uint8_t buf[smi::kStorageSize]{};
    size_t actual = 0;
    for (int retry = 0; retry < 200; retry++) {
        rc = popInStorage(buf, sizeof(buf), &actual);
        if (R_SUCCEEDED(rc)) break;
        svcSleepThread(10'000'000ULL);
    }
    if (R_FAILED(rc)) return rc;

    if (actual >= sizeof(smi::CommandHeader) + sizeof(smi::SystemStatus)) {
        std::memcpy(&out, buf + sizeof(smi::CommandHeader), sizeof(smi::SystemStatus));
    }
    return 0;
}

}
