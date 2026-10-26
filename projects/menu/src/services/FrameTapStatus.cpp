#include "FrameTapStatus.hpp"

#include <cstdio>
#include <cstring>

namespace switchu::services {

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
            m_status = {};
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
        m_status = {};
        return false;
    }

    m_available = true;
    m_status = st;
    const bool netViewer = (st.reserved3 & kReserved3NetViewer) != 0;
    const bool streaming = st.state == LiveState_Streaming;
    m_viewerConnected = m_forceLight || netViewer || streaming;
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

} // namespace switchu::services
