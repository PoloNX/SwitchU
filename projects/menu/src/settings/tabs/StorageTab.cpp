#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#ifndef SWITCHU_HOMEBREW
#include <nxtc.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {

std::string formatBytes(uint64_t bytes) {
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        double value = bytes / (1024.0 * 1024.0 * 1024.0);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f GB", value);
        return buf;
    }
    if (bytes >= 1024ull * 1024ull) {
        double value = bytes / (1024.0 * 1024.0);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f MB", value);
        return buf;
    }
    if (bytes >= 1024ull) {
        double value = bytes / 1024.0;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f KB", value);
        return buf;
    }
    return std::to_string(bytes) + " B";
}

bool queryStorageSize(NcmStorageId storageId, uint64_t& total, uint64_t& freeSpace) {
    s64 totalSize = 0;
    s64 freeSize  = 0;
    if (R_FAILED(nsGetStorageSize(storageId, &totalSize, &freeSize)))
        return false;
    if (totalSize < 0 || freeSize < 0)
        return false;
    total = static_cast<uint64_t>(totalSize);
    freeSpace = static_cast<uint64_t>(freeSize);
    return true;
}

uint64_t queryApplicationSize(uint64_t titleId) {
    NsApplicationOccupiedSize occ{};
    if (R_FAILED(nsCalculateApplicationOccupiedSize(titleId, &occ)))
        return 0;

    uint64_t bestSize = 0;
    for (size_t offset = 0; offset + sizeof(uint64_t) <= sizeof(occ.unk_x0); offset += sizeof(uint64_t)) {
        uint64_t candidate = 0;
        std::memcpy(&candidate, &occ.unk_x0[offset], sizeof(candidate));
        if (candidate > bestSize && candidate < (1ull << 40)) {
            bestSize = candidate;
        }
    }

    return bestSize;
}

std::string storageLocationLabel(const nxui::I18n& i18n, u8 storageId);

std::string applicationStorageLocation(uint64_t titleId, const nxui::I18n& i18n) {
    NsApplicationContentMetaStatus statuses[64] = {};
    s32 count = 0;
    if (R_FAILED(nsListApplicationContentMetaStatus(titleId, 0, statuses, 64, &count)) || count <= 0)
        return std::string();

    for (int i = 0; i < count; ++i) {
        switch (static_cast<NcmStorageId>(statuses[i].storageID)) {
            case NcmStorageId_SdCard:
                return storageLocationLabel(i18n, statuses[i].storageID);
            case NcmStorageId_BuiltInUser:
                return storageLocationLabel(i18n, statuses[i].storageID);
            case NcmStorageId_BuiltInSystem:
                return storageLocationLabel(i18n, statuses[i].storageID);
            default:
                break;
        }
    }

    return std::string();
}

std::string formatAppOption(const std::string& title,
                            const std::string& location,
                            const std::string& sizeText,
                            const std::string& status) {
    std::string result = title;
    if (!location.empty())
        result += " — " + location;
    if (!sizeText.empty())
        result += " • " + sizeText;
    if (!status.empty())
        result += " • " + status;
    return result;
}

std::string formatAppPreview(const std::string& title,
                             const std::string& location,
                             const std::string& sizeText) 
{
    std::string result = title;

    if (!location.empty())
        result += " — " + location;

    if (!sizeText.empty())
        result += " — " + sizeText;

    return result;
}


std::string formatAppDetails(const std::string& location,
                             const std::string& sizeText,
                             const std::string& status) {
    std::string result;
    if (!location.empty())
        result += location;
    if (!sizeText.empty()) {
        if (!result.empty()) result += " • ";
        result += sizeText;
    }
    if (!status.empty()) {
        if (!result.empty()) result += " • ";
        result += status;
    }
    return result;
}

enum class AppState {
    Installed,
    Corrupt,
    Unknown,
};

struct AppControlInfo {
    std::string title;
    AppState state;
};

AppControlInfo queryApplicationControlInfo(uint64_t titleId) {
    AppControlInfo info;

#ifndef SWITCHU_HOMEBREW
    NxTitleCacheApplicationMetadata* meta = nxtcGetApplicationMetadataEntryById(titleId);
    if (meta) {
        if (meta->name && meta->name[0] != '\0')
            info.title = meta->name;
        else
            info.title = std::string();
        info.state = AppState::Installed;
        nxtcFreeApplicationMetadata(&meta);
        if (!info.title.empty())
            return info;
    }
#endif

    NsApplicationControlData controlData{};
    size_t actualSize = 0;
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                               titleId,
                                               &controlData,
                                               sizeof(controlData),
                                               &actualSize))) {
        NacpLanguageEntry* entry = nullptr;
        if (R_SUCCEEDED(nacpGetLanguageEntry(&controlData.nacp, &entry)) &&
            entry && entry->name[0] != '\0') {
            info.title = entry->name;
        } else {
            for (int i = 0; i < 16; ++i) {
                if (controlData.nacp.lang[i].name[0] != '\0') {
                    info.title = controlData.nacp.lang[i].name;
                    break;
                }
            }
        }
        info.state = AppState::Installed;
        if (info.title.empty()) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%016llX", (unsigned long long)titleId);
            info.title = buffer;
        }
        return info;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llX", (unsigned long long)titleId);
    info.title = buffer;
    info.state = AppState::Corrupt;
    return info;
}

std::string titleForApplication(uint64_t titleId) {
    return queryApplicationControlInfo(titleId).title;
}

AppState queryApplicationState(uint64_t titleId) {
    return queryApplicationControlInfo(titleId).state;
}

std::string appStateLabel(const nxui::I18n& i18n, AppState state) {
    switch (state) {
        case AppState::Installed:
            return i18n.tr("settings.storage.state_installed", "Installed");
        case AppState::Corrupt:
            return i18n.tr("settings.storage.state_corrupt", "Corrupt");
        default:
            return i18n.tr("settings.storage.state_unknown", "Unknown");
    }
}

std::string storageLocationLabel(const nxui::I18n& i18n, u8 storageId) {
    switch (static_cast<NcmStorageId>(storageId)) {
        case NcmStorageId_BuiltInUser:
            return i18n.tr("settings.storage.location_internal", "Internal");
        case NcmStorageId_SdCard:
            return i18n.tr("settings.storage.location_sd", "microSD");
        case NcmStorageId_BuiltInSystem:
            return i18n.tr("settings.storage.location_system", "System");
        default:
            return i18n.tr("common.na", "N/A");
    }
}

}

SettingsScreen::Tab settings::tabs::StorageTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("settings.tabs.storage", "Storage");

    uint64_t internalTotal = 0;
    uint64_t internalFree  = 0;
    bool internalOk = queryStorageSize(NcmStorageId_BuiltInUser, internalTotal, internalFree);
    uint64_t internalUsed = internalOk ? (internalTotal - internalFree) : 0;

    auto makeStorageSummary = [&](const std::string& totalText, const std::string& freeText, const std::string& usedText) {
        return i18n.tr("settings.storage.capacity", "Capacity") + ": " + totalText
             + " • " + i18n.tr("settings.storage.available", "Available") + ": " + freeText
             + " • " + i18n.tr("settings.storage.used", "Used") + ": " + usedText;
    };

    SettingItem internalBar;
    internalBar.label = i18n.tr("settings.storage.internal", "System Memory");
    internalBar.type = ItemType::Progress;
    internalBar.description = internalOk
        ? i18n.tr("settings.storage.internal_desc", "Shows used system memory and available storage.")
        : i18n.tr("settings.storage.internal_unavailable", "Unable to query internal storage.");
    internalBar.floatVal = internalOk && internalTotal > 0 ? float((double)internalUsed / internalTotal) : 0.f;
    internalBar.anim01 = internalBar.floatVal;
    internalBar.infoText = internalOk
        ? formatBytes(internalUsed) + " / " + formatBytes(internalTotal)
        : i18n.tr("common.na", "N/A");
    t.items.push_back(std::move(internalBar));

    SettingItem internalSummary;
    internalSummary.label = i18n.tr("settings.storage.summary", "Summary");
    internalSummary.type = ItemType::Info;
    internalSummary.infoText = internalOk
        ? makeStorageSummary(formatBytes(internalTotal), formatBytes(internalFree), formatBytes(internalUsed))
        : i18n.tr("common.na", "N/A");
    t.items.push_back(std::move(internalSummary));

    uint64_t sdTotal = 0;
    uint64_t sdFree  = 0;
    bool sdOk = queryStorageSize(NcmStorageId_SdCard, sdTotal, sdFree);
    uint64_t sdUsed = sdOk ? (sdTotal - sdFree) : 0;

    SettingItem sdBar;
    sdBar.label = i18n.tr("settings.storage.sd", "microSD Card");
    sdBar.type  = ItemType::Progress;
    sdBar.description = sdOk
        ? i18n.tr("settings.storage.sd_desc", "Shows used microSD storage and available storage.")
        : i18n.tr("settings.storage.sd_unavailable", "No microSD card detected or access denied.");
    sdBar.floatVal = sdOk && sdTotal > 0 ? float((double)sdUsed / sdTotal) : 0.f;
    sdBar.anim01 = sdBar.floatVal;
    sdBar.infoText = sdOk
        ? formatBytes(sdUsed) + " / " + formatBytes(sdTotal)
        : i18n.tr("common.na", "N/A");
    t.items.push_back(std::move(sdBar));

    SettingItem sdSummary;
    sdSummary.label = i18n.tr("settings.storage.summary", "Summary");
    sdSummary.type = ItemType::Info;
    sdSummary.infoText = sdOk
        ? makeStorageSummary(formatBytes(sdTotal), formatBytes(sdFree), formatBytes(sdUsed))
        : i18n.tr("common.na", "N/A");
    t.items.push_back(std::move(sdSummary));

    SettingItem section;
    section.label = i18n.tr("settings.storage.installed_section", "Installed Software");
    section.type = ItemType::Section;
    t.items.push_back(std::move(section));

    struct AppEntry {
        uint64_t titleId;
        std::string title;
        std::string location;
        std::string sizeText;
        AppState  state;
    };

    std::vector<AppEntry> apps;
    NsApplicationRecord records[1024] = {};
    s32 recordCount = 0;
    if (R_SUCCEEDED(nsListApplicationRecord(records, 1024, 0, &recordCount)) && recordCount > 0) {
        for (int i = 0; i < recordCount; ++i) {
            uint64_t titleId = records[i].application_id;
            AppEntry app;
            app.titleId = titleId;
            auto controlInfo = queryApplicationControlInfo(titleId);
            app.title = std::move(controlInfo.title);
            app.state = controlInfo.state;
            app.location = applicationStorageLocation(titleId, i18n);
            uint64_t sizeBytes = queryApplicationSize(titleId);
            if (sizeBytes > 0) {
                app.sizeText = formatBytes(sizeBytes);
            } else {
                app.sizeText = i18n.tr("settings.storage.size_unknown", "Unknown size");
            }
            apps.push_back(std::move(app));
        }
    }

    std::sort(apps.begin(), apps.end(), [](const AppEntry& a, const AppEntry& b) {
        return a.title < b.title;
    });

    if (apps.empty()) {
        SettingItem it;
        it.label = i18n.tr("settings.storage.empty_library", "No installed games found");
        it.type = ItemType::Info;
        it.infoText = "";
        t.items.push_back(std::move(it));
        return t;
    }

    auto selectedIndex = std::make_shared<int>(0);
    auto appsPtr = std::make_shared<std::vector<AppEntry>>(std::move(apps));
    t.items.reserve(t.items.size() + 6);

    auto& selector = t.items.emplace_back();
    selector.label = i18n.tr("settings.storage.select_game", "Installed Game");
    selector.type = ItemType::Selector;
    selector.description = i18n.tr("settings.storage.select_game_desc", "Choose a title to uninstall from the system.");
    selector.intVal = 0;
    selector.options.reserve(appsPtr->size());
    for (auto& app : *appsPtr) {
        selector.options.push_back(formatAppPreview(app.title, app.location, app.sizeText));
    }

    auto& selectedTitle = t.items.emplace_back();
    selectedTitle.label = i18n.tr("settings.storage.selected_title", "Selected Title");
    selectedTitle.type = ItemType::Info;
    selectedTitle.infoText = (*appsPtr)[0].title;

    auto& selectedDetails = t.items.emplace_back();
    selectedDetails.label = i18n.tr("settings.storage.details", "Details");
    selectedDetails.type = ItemType::Info;
    selectedDetails.infoText = formatAppDetails((*appsPtr)[0].location,
                                               (*appsPtr)[0].sizeText,
                                               appStateLabel(i18n, (*appsPtr)[0].state));

    selector.onChange = [selectedIndex, appsPtr, &i18n, &selectedTitle, &selectedDetails](SettingItem& self) {
        int newIndex = std::clamp(self.intVal, 0, (int)self.options.size() - 1);
        *selectedIndex = newIndex;
        const auto& app = (*appsPtr)[newIndex];
        selectedTitle.infoText = app.title;
        selectedDetails.infoText = formatAppDetails(app.location,
                                                   app.sizeText,
                                                   appStateLabel(i18n, app.state));
    };

    auto& uninstall = t.items.emplace_back();
    uninstall.label = i18n.tr("settings.storage.uninstall_game", "Uninstall Selected Game");
    uninstall.type = ItemType::Action;
    uninstall.description = i18n.tr("settings.storage.uninstall_game_desc", "Permanently delete the selected title from the system.");
    uninstall.onChange = [&screen, appsPtr, selectedIndex, &i18n](SettingItem& /* self */) {
        int index = std::clamp(*selectedIndex, 0, (int)appsPtr->size() - 1);
        const auto app = (*appsPtr)[index];
        auto message = i18n.tr("settings.storage.uninstall_confirm_msg", "Delete %s from the system?");
        size_t pos = message.find("%s");
        if (pos != std::string::npos) {
            std::string titleText = app.title;
            message.replace(pos, 2, titleText);
        }

        screen.requestDialog(
            i18n.tr("settings.storage.uninstall_confirm_title", "Uninstall Game"),
            message,
            {
                { i18n.tr("button.delete", "Delete"), [app, &screen, &i18n]() {
                    Result rc = nsDeleteApplicationCompletely(app.titleId);
                    if (R_SUCCEEDED(rc)) {
                        screen.rebuildCurrentTab();
                        screen.requestToast(i18n.tr("settings.storage.uninstall_success", "Uninstalled successfully."), 2.8f);
                    } else {
                        screen.requestDialog(
                            i18n.tr("settings.storage.uninstall_failed_title", "Uninstall Failed"),
                            i18n.tr("settings.storage.uninstall_failed", "Failed to uninstall the selected title."),
                            {{ i18n.tr("button.ok", "OK"), []() {} }});
                    }
                } },
                { i18n.tr("button.cancel", "Cancel"), []() {} }
            });
    };

    return t;
}
