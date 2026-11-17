#include "LockAuthStore.hpp"

#include "core/DebugLog.hpp"

#include <switch.h>
#include <switch/crypto/sha256.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace switchu::lockauth {

void LockAuthStore::hashSecret(const std::uint8_t* salt, const SecretBytes& secret,
                               std::uint8_t out[kHashSize]) {
    Sha256Context ctx{};
    sha256ContextCreate(&ctx);
    sha256ContextUpdate(&ctx, salt, kSaltSize);
    if (!secret.empty())
        sha256ContextUpdate(&ctx, secret.data(), secret.size());
    sha256ContextGetHash(&ctx, out);
}

bool LockAuthStore::writeFile(const FileHeader& hdr) const {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(kAuthFilePath).parent_path(), ec);
    std::ofstream out(kAuthFilePath, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    return static_cast<bool>(out);
}

bool LockAuthStore::readFile(FileHeader& hdr) const {
    std::ifstream in(kAuthFilePath, std::ios::binary);
    if (!in)
        return false;
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    return in.good() && in.gcount() == static_cast<std::streamsize>(sizeof(hdr));
}

void LockAuthStore::load(Mode settingsMode) {
    m_mode = Mode::Off;
    m_haveHash = false;
    m_failsafeCleared = false;
    m_pinLength = kDefaultPinLength;
    m_salt.fill(0);
    m_hash.fill(0);

    FileHeader hdr{};
    const bool haveFile = readFile(hdr);
    if (!haveFile) {
        if (settingsMode != Mode::Off) {
            m_failsafeCleared = true;
            DebugLog::log("[lockauth] failsafe: settings had mode=%u but %s missing",
                          static_cast<unsigned>(settingsMode), kAuthFilePath);
        }
        return;
    }

    if (hdr.magic != kFileMagic || hdr.version != kFileVersion) {
        DebugLog::log("[lockauth] invalid auth file — treating as Off");
        return;
    }

    const Mode fileMode = modeFromInt(hdr.mode);
    if (fileMode == Mode::Off) {
        return;
    }

    m_mode = fileMode;
    m_pinLength = std::clamp(static_cast<int>(hdr.pinLength), kMinPinLength, kMaxPinLength);
    std::memcpy(m_salt.data(), hdr.salt, kSaltSize);
    std::memcpy(m_hash.data(), hdr.hash, kHashSize);
    m_haveHash = true;

    if (settingsMode != Mode::Off && settingsMode != fileMode) {
        // Prefer the secret file as source of truth for the active method.
        DebugLog::log("[lockauth] settings mode=%u file mode=%u — using file",
                      static_cast<unsigned>(settingsMode),
                      static_cast<unsigned>(fileMode));
    }
}

bool LockAuthStore::enroll(Mode mode, const SecretBytes& secret, int pinLength) {
    if (mode == Mode::Off || secret.empty())
        return false;

    FileHeader hdr{};
    hdr.magic = kFileMagic;
    hdr.version = kFileVersion;
    hdr.mode = static_cast<std::uint8_t>(mode);
    hdr.pinLength = static_cast<std::uint8_t>(
        std::clamp(pinLength, kMinPinLength, kMaxPinLength));

    std::random_device rd;
    for (std::size_t i = 0; i < kSaltSize; ++i)
        hdr.salt[i] = static_cast<std::uint8_t>(rd() & 0xFF);

    hashSecret(hdr.salt, secret, hdr.hash);

    if (!writeFile(hdr)) {
        DebugLog::log("[lockauth] enroll write FAIL");
        return false;
    }

    m_mode = mode;
    m_pinLength = hdr.pinLength;
    std::memcpy(m_salt.data(), hdr.salt, kSaltSize);
    std::memcpy(m_hash.data(), hdr.hash, kHashSize);
    m_haveHash = true;
    m_failsafeCleared = false;
    DebugLog::log("[lockauth] enrolled mode=%u", static_cast<unsigned>(mode));
    return true;
}

bool LockAuthStore::verify(const SecretBytes& secret) const {
    if (!m_haveHash || m_mode == Mode::Off)
        return false;
    std::uint8_t got[kHashSize]{};
    hashSecret(m_salt.data(), secret, got);
    return std::memcmp(got, m_hash.data(), kHashSize) == 0;
}

bool LockAuthStore::clear() {
    std::remove(kAuthFilePath);
    m_mode = Mode::Off;
    m_haveHash = false;
    m_pinLength = kDefaultPinLength;
    m_salt.fill(0);
    m_hash.fill(0);
    DebugLog::log("[lockauth] cleared");
    return true;
}

} // namespace switchu::lockauth
