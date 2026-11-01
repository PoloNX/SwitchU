#include "FrameTapStatus.hpp"

#include <cstdio>
#include <cstring>

namespace switchu::services {

namespace {

// sm defers GetService for a name nobody has registered: the call blocks until
// the service appears. Without Frame Tap installed that is forever, and the
// first probe runs at the top of WiiUMenuApp::onCreate, so HOME hangs on a
// black screen before its first frame. Ask Atmosphere's sm whether "sftap"
// exists first (tipc 65100, the same call libstratosphere's
// smAtmosphereHasService makes; libnx no longer ships it) and treat any
// failure as "not installed".
bool sftapRegistered() {
    u8 has = 0;
    const SmServiceName name = smEncodeName("sftap");
    const Result rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, has);
    return R_SUCCEEDED(rc) && (has & 1) != 0;
}

} // namespace

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
        const Result rc = sftapRegistered() ? smGetService(&svc, "sftap")
                                            : MAKERESULT(Module_Libnx, LibnxError_NotFound);
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
    // Net viewer bit is durable; Streaming covers USB / older FT. Also treat
    // WaitViewer with the net bit as connected (viewer present, not yet live).
    m_viewerConnected = m_forceLight || netViewer || streaming;
    m_needsHeadroom = (st.reserved3 & kReserved3NeedHeadroom) != 0;
    m_headroomNeedMb = 0;
    if (m_needsHeadroom) {
        // Requested MB via GetDonorCap (cmd 13) — not shot_fails (overlay/app).
        std::uint32_t mb = 0;
        if (R_SUCCEEDED(serviceDispatchOut(&m_service, 13, mb)) && mb > 0)
            m_headroomNeedMb = mb;
        else
            pollNeedHeadroomFile();
        if (m_headroomNeedMb == 0)
            m_headroomNeedMb = 30;
    } else {
        pollNeedHeadroomFile();
    }
    return true;
}

bool FrameTapStatus::probe(float dtSeconds, float pollIntervalMs) {
    m_pollAccumMs += dtSeconds * 1000.f;
    // File fallback must not wait on sftap open/backoff — FT often writes
    // need_headroom while detach-on-home briefly drops the service.
    if (!m_needsHeadroom)
        pollNeedHeadroomFile();
    if (!m_serviceOpen) {
        // Back off when FT is absent so we do not spam smGetService every frame.
        // Cap at pollIntervalMs so cold-boot remount (100ms poll) can catch FT
        // the moment sftap appears — before the viewer→seed race.
        m_retryAccumMs += dtSeconds * 1000.f;
        const float retryMs = pollIntervalMs > 0.f ? pollIntervalMs : 500.f;
        if (m_retryAccumMs < retryMs && m_pollAccumMs < pollIntervalMs)
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
        if (!sftapRegistered() || R_FAILED(smGetService(&svc, "sftap")))
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
