#include "FrameTapStatus.hpp"

#include <cstdio>
#include <cstring>

namespace switchu::services {

void FrameTapStatus::pollNeedHeadroomFile() {
    FILE* f = std::fopen(kNeedHeadroomFile, "rb");
    if (!f)
        return;
    unsigned mb = 0;
    if (std::fscanf(f, "%u", &mb) == 1 && mb > 0) {
        m_needsHeadroom = true;
        if (m_headroomNeedMb == 0)
            m_headroomNeedMb = mb;
    } else {
        m_needsHeadroom = true;
        if (m_headroomNeedMb == 0)
            m_headroomNeedMb = 30;
    }
    std::fclose(f);
}

bool FrameTapStatus::refresh() {
    m_forceLight = false;
    {
        FILE* f = std::fopen(kForceLightPath, "rb");
        if (f) {
            std::fclose(f);
            m_forceLight = true;
        }
    }

    if (!m_serviceOpen) {
        m_retryAccumMs = 0.f;
        Service svc{};
        const Result rc = smGetService(&svc, "sftap");
        if (R_FAILED(rc)) {
            m_available = false;
            m_viewerConnected = m_forceLight;
            m_needsHeadroom = false;
            m_headroomNeedMb = 0;
            m_status = {};
            pollNeedHeadroomFile();
            return false;
        }
        m_service = svc;
        m_serviceOpen = true;
    }

    StreamStatus st{};
    const Result rc = serviceDispatchOut(&m_service, 0, st);
    if (R_FAILED(rc)) {
        serviceClose(&m_service);
        m_service = {};
        m_serviceOpen = false;
        m_available = false;
        m_viewerConnected = m_forceLight;
        m_needsHeadroom = false;
        m_headroomNeedMb = 0;
        m_status = {};
        pollNeedHeadroomFile();
        return false;
    }

    m_available = true;
    m_status = st;
    const bool netViewer = (st.reserved3 & kReserved3NetViewer) != 0;
    const bool streaming = st.state == LiveState_Streaming;
    m_viewerConnected = m_forceLight || netViewer || streaming;
    m_needsHeadroom = (st.reserved3 & kReserved3NeedHeadroom) != 0;
    m_headroomNeedMb = m_needsHeadroom ? st.shot_fails : 0;
    if (!m_needsHeadroom)
        pollNeedHeadroomFile();
    return true;
}

bool FrameTapStatus::probe(float dtSeconds, float pollIntervalMs) {
    m_pollAccumMs += dtSeconds * 1000.f;
    if (!m_serviceOpen) {
        // Back off when FT is absent so we do not spam smGetService every frame.
        m_retryAccumMs += dtSeconds * 1000.f;
        if (m_retryAccumMs < 2000.f && m_pollAccumMs < pollIntervalMs)
            return false;
        m_retryAccumMs = 0.f;
        m_pollAccumMs = 0.f;
        return refresh();
    }
    if (m_pollAccumMs < pollIntervalMs)
        return false;
    m_pollAccumMs = 0.f;
    return refresh();
}

bool FrameTapStatus::ackHeadroom(std::uint32_t freeKb) {
    // File fallback when FT is old / service briefly unavailable.
    std::remove(kNeedHeadroomFile);
    {
        FILE* f = std::fopen(kHeadroomReadyFile, "wb");
        if (f) {
            std::fprintf(f, "%u\n", freeKb);
            std::fclose(f);
        }
    }

    if (!m_serviceOpen) {
        Service svc{};
        if (R_FAILED(smGetService(&svc, "sftap")))
            return false;
        m_service = svc;
        m_serviceOpen = true;
    }
    const Result rc = serviceDispatchIn(&m_service, 15, freeKb);
    if (R_FAILED(rc)) {
        serviceClose(&m_service);
        m_service = {};
        m_serviceOpen = false;
        return false;
    }
    m_needsHeadroom = false;
    return true;
}

} // namespace switchu::services
