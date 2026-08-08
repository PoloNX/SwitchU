#pragma once
#include <switchu/smi_protocol.hpp>
#include <switch.h>
#include <cstring>
#include <vector>

namespace switchu::smi {

class StorageWriter {
public:
    explicit StorageWriter(SystemMessage msg) {
        m_buf.resize(kStorageSize, 0);
        CommandHeader hdr{kCommandMagic, static_cast<uint32_t>(msg), 0};
        std::memcpy(m_buf.data(), &hdr, sizeof(hdr));
        m_pos = sizeof(CommandHeader);
    }

    explicit StorageWriter(Result rc) {
        m_buf.resize(kStorageSize, 0);
        CommandHeader hdr{kCommandMagic, static_cast<uint32_t>(rc), 0};
        std::memcpy(m_buf.data(), &hdr, sizeof(hdr));
        m_pos = sizeof(CommandHeader);
    }

    template<typename T>
    void push(const T& val) {
        if (m_pos + sizeof(T) > m_buf.size()) return;
        std::memcpy(m_buf.data() + m_pos, &val, sizeof(T));
        m_pos += sizeof(T);
    }

    void pushBytes(const void* data, size_t len) {
        if (m_pos + len > m_buf.size()) return;
        std::memcpy(m_buf.data() + m_pos, data, len);
        m_pos += len;
    }

    Result createStorage(AppletStorage& st) const {
        Result rc = appletCreateStorage(&st, kStorageSize);
        if (R_SUCCEEDED(rc)) {
            rc = appletStorageWrite(&st, 0, m_buf.data(), kStorageSize);
            if (R_FAILED(rc))
                appletStorageClose(&st);
        }
        return rc;
    }

private:
    std::vector<uint8_t> m_buf;
    size_t m_pos = 0;
};

class StorageReader {
public:
    explicit StorageReader(AppletStorage& st) {
        m_buf.resize(kStorageSize);
        s64 sz = 0;
        appletStorageGetSize(&st, &sz);
        if (sz > 0) {
            if ((size_t)sz < kStorageSize)
                m_buf.resize((size_t)sz);
            appletStorageRead(&st, 0, m_buf.data(), m_buf.size());
        }
        appletStorageClose(&st);
        m_pos = sizeof(CommandHeader);
    }

    bool valid() const {
        if (m_buf.size() < sizeof(CommandHeader)) return false;
        CommandHeader hdr;
        std::memcpy(&hdr, m_buf.data(), sizeof(hdr));
        return hdr.magic == kCommandMagic;
    }

    uint32_t messageOrResult() const {
        CommandHeader hdr;
        std::memcpy(&hdr, m_buf.data(), sizeof(hdr));
        return hdr.message;
    }

    SystemMessage systemMessage() const {
        return static_cast<SystemMessage>(messageOrResult());
    }

    uint64_t requestId() const {
        CommandHeader hdr{};
        if (m_buf.size() >= sizeof(hdr))
            std::memcpy(&hdr, m_buf.data(), sizeof(hdr));
        return hdr.request_id;
    }

    template<typename T>
    T pop() {
        T val{};
        if (m_pos + sizeof(T) <= m_buf.size()) {
            std::memcpy(&val, m_buf.data() + m_pos, sizeof(T));
            m_pos += sizeof(T);
        }
        return val;
    }

    const uint8_t* rawAt(size_t offset) const {
        if (offset >= m_buf.size()) return nullptr;
        return m_buf.data() + offset;
    }

    size_t remaining() const {
        return (m_pos < m_buf.size()) ? m_buf.size() - m_pos : 0;
    }

    size_t position() const { return m_pos; }

private:
    std::vector<uint8_t> m_buf;
    size_t m_pos = 0;
};

using StorageFn = Result(*)(AppletStorage*);

inline Result loopWaitStorage(StorageFn fn, AppletStorage* st, bool wait) {
    if (!wait) return fn(st);
    for (uint32_t i = 0; i < kMaxRetries; ++i) {
        if (R_SUCCEEDED(fn(st))) return 0;
        svcSleepThread(kRetrySleepNs);
    }
    return MAKERESULT(Module_Libnx, 0xFF);
}

}
