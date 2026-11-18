#pragma once

#include "LockAuthTypes.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace switchu::lockauth {

class LockAuthStore {
public:
    /// Load secret file + reconcile with settings mode. Missing file → Off.
    void load(Mode settingsMode);

    Mode mode() const { return m_mode; }
    bool isEnabled() const { return m_mode != Mode::Off && m_haveHash; }
    int pinLength() const { return m_pinLength; }

    /// Persist a new enrollment. Overwrites lock_auth.bin.
    bool enroll(Mode mode, const SecretBytes& secret, int pinLength = kDefaultPinLength);

    bool verify(const SecretBytes& secret) const;

    /// Clear secret file and set mode Off.
    bool clear();

    /// True when settings said enabled but file was deleted (failsafe).
    bool failsafeCleared() const { return m_failsafeCleared; }
    void acknowledgeFailsafe() { m_failsafeCleared = false; }

private:
#pragma pack(push, 1)
    struct FileHeader {
        std::uint32_t magic = kFileMagic;
        std::uint16_t version = kFileVersion;
        std::uint8_t mode = 0;
        std::uint8_t pinLength = kDefaultPinLength;
        std::uint8_t salt[kSaltSize]{};
        std::uint8_t hash[kHashSize]{};
        std::uint8_t reserved[8]{};
    };
#pragma pack(pop)
    static_assert(sizeof(FileHeader) == 64);

    static void hashSecret(const std::uint8_t* salt, const SecretBytes& secret,
                           std::uint8_t out[kHashSize]);
    bool writeFile(const FileHeader& hdr) const;
    bool readFile(FileHeader& hdr) const;

    Mode m_mode = Mode::Off;
    int m_pinLength = kDefaultPinLength;
    bool m_haveHash = false;
    bool m_failsafeCleared = false;
    std::array<std::uint8_t, kSaltSize> m_salt{};
    std::array<std::uint8_t, kHashSize> m_hash{};
};

} // namespace switchu::lockauth
