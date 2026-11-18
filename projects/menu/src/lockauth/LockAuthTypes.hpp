#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace switchu::lockauth {

enum class Mode : std::uint8_t {
    Off = 0,
    Pin = 1,
    Password = 2,
    Pattern = 3,
    Combo = 4,
};

inline constexpr const char* kAuthFilePath = "sdmc:/config/SwitchU/lock_auth.bin";
inline constexpr std::uint32_t kFileMagic = 0x53554C41; // 'SULA'
inline constexpr std::uint16_t kFileVersion = 1;
inline constexpr int kDefaultPinLength = 4;
inline constexpr int kMinPinLength = 4;
inline constexpr int kMaxPinLength = 8;
inline constexpr int kMinPasswordLength = 4;
inline constexpr int kMaxPasswordLength = 64;
inline constexpr int kPatternCells = 9;
inline constexpr int kMinPatternLen = 4;
inline constexpr int kMaxPatternLen = 9;
inline constexpr int kMinComboLen = 4;
inline constexpr int kMaxComboLen = 12;
inline constexpr int kFailLimit = 5;
inline constexpr float kFailLockoutSec = 30.f;
inline constexpr std::size_t kSaltSize = 16;
inline constexpr std::size_t kHashSize = 32;

inline const char* modeLabel(Mode mode) {
    switch (mode) {
        case Mode::Pin: return "PIN";
        case Mode::Password: return "Password";
        case Mode::Pattern: return "Pattern";
        case Mode::Combo: return "Combo";
        case Mode::Off:
        default: return "Off";
    }
}

inline Mode modeFromInt(int v) {
    if (v < 0 || v > 4) return Mode::Off;
    return static_cast<Mode>(v);
}

/// Normalized secret bytes used for hashing (not logged).
using SecretBytes = std::vector<std::uint8_t>;

inline SecretBytes secretFromPin(const std::string& digits) {
    return SecretBytes(digits.begin(), digits.end());
}

inline SecretBytes secretFromPassword(const std::string& text) {
    return SecretBytes(text.begin(), text.end());
}

inline SecretBytes secretFromPattern(const std::vector<std::uint8_t>& cells) {
    return SecretBytes(cells.begin(), cells.end());
}

inline SecretBytes secretFromCombo(const std::vector<std::uint32_t>& buttons) {
    SecretBytes out;
    out.reserve(buttons.size() * 4);
    for (std::uint32_t b : buttons) {
        out.push_back(static_cast<std::uint8_t>(b & 0xFF));
        out.push_back(static_cast<std::uint8_t>((b >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((b >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((b >> 24) & 0xFF));
    }
    return out;
}

} // namespace switchu::lockauth
