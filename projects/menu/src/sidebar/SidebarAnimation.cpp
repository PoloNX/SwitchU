#include "SidebarAnimation.hpp"
#include "core/DebugLog.hpp"
#include <webp/demux.h>
#include <fstream>
#include <cmath>
#include <algorithm>

bool SidebarAnimation::load(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                            const std::string& webpPath) {
    m_frames.clear();
    m_durationsMs.clear();
    m_frameIndex  = 0;
    m_elapsedMs   = 0.f;

    std::ifstream input(webpPath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        DebugLog::log("[sidebar-anim] not found: %s", webpPath.c_str());
        return false;
    }

    const std::streamoff sz = input.tellg();
    if (sz <= 0) return false;
    input.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return false;

    WebPData webpData;
    webpData.bytes = bytes.data();
    webpData.size  = bytes.size();

    WebPAnimDecoderOptions opts;
    if (!WebPAnimDecoderOptionsInit(&opts)) return false;
    opts.color_mode = MODE_RGBA;

    WebPAnimDecoder* dec = WebPAnimDecoderNew(&webpData, &opts);
    if (!dec) return false;

    WebPAnimInfo info;
    if (!WebPAnimDecoderGetInfo(dec, &info) ||
        info.canvas_width <= 0 || info.canvas_height <= 0) {
        WebPAnimDecoderDelete(dec);
        return false;
    }

    constexpr int kHardMaxSide = 2048;
    if (static_cast<int>(info.canvas_width)  > kHardMaxSide ||
        static_cast<int>(info.canvas_height) > kHardMaxSide) {
        DebugLog::log("[sidebar-anim] canvas too large (%dx%d)",
                      info.canvas_width, info.canvas_height);
        WebPAnimDecoderDelete(dec);
        return false;
    }

    constexpr int kMaxUploadSide = 64;
    int uploadW = static_cast<int>(info.canvas_width);
    int uploadH = static_cast<int>(info.canvas_height);
    bool needScale = (uploadW > kMaxUploadSide || uploadH > kMaxUploadSide);
    if (needScale) {
        float scale = std::min(static_cast<float>(kMaxUploadSide) / uploadW,
                               static_cast<float>(kMaxUploadSide) / uploadH);
        uploadW = std::max(1, static_cast<int>(std::round(uploadW * scale)));
        uploadH = std::max(1, static_cast<int>(std::round(uploadH * scale)));
    }

    std::vector<uint8_t> scaledBuf;
    if (needScale)
        scaledBuf.resize(static_cast<size_t>(uploadW) * uploadH * 4u);

    constexpr size_t kMaxGpuBytes = 2u * 1024u * 1024u;
    size_t bytesPerFrame = static_cast<size_t>(uploadW) * uploadH * 4u;
    size_t maxFrames = std::max<size_t>(1u, kMaxGpuBytes / std::max<size_t>(1u, bytesPerFrame));
    size_t reserve   = std::min<size_t>(maxFrames, std::max<size_t>(1u, info.frame_count));
    m_frames.reserve(reserve);
    m_durationsMs.reserve(reserve);

    int prevTs = 0;
    while (WebPAnimDecoderHasMoreFrames(dec)) {
        uint8_t* rgba = nullptr;
        int timestamp  = 0;
        if (!WebPAnimDecoderGetNext(dec, &rgba, &timestamp) || !rgba)
            break;
        if (m_frames.size() >= reserve)
            break;

        const uint8_t* uploadPixels = rgba;
        if (needScale) {
            for (int y = 0; y < uploadH; ++y) {
                int srcY = (y * static_cast<int>(info.canvas_height)) / uploadH;
                const uint8_t* srcRow = rgba + static_cast<size_t>(srcY) * info.canvas_width * 4u;
                uint8_t* dstRow = scaledBuf.data() + static_cast<size_t>(y) * uploadW * 4u;
                for (int x = 0; x < uploadW; ++x) {
                    int srcX = (x * static_cast<int>(info.canvas_width)) / uploadW;
                    const uint8_t* s = srcRow + static_cast<size_t>(srcX) * 4u;
                    uint8_t* d = dstRow + static_cast<size_t>(x) * 4u;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            }
            uploadPixels = scaledBuf.data();
        }

        nxui::Texture tex;
        if (tex.loadFromPixelsPooled(gpu, ren, uploadPixels, uploadW, uploadH)) {
            int durationMs = timestamp - prevTs;
            if (durationMs <= 0) durationMs = 1;
            m_frames.push_back(std::move(tex));
            m_durationsMs.push_back(durationMs);
        } else {
            DebugLog::log("[sidebar-anim] frame upload failed at %d",
                          static_cast<int>(m_frames.size()));
            break;
        }
        prevTs = timestamp;
    }

    WebPAnimDecoderDelete(dec);

    if (m_frames.empty()) {
        DebugLog::log("[sidebar-anim] decode failed: %s", webpPath.c_str());
        return false;
    }
    if (!m_durationsMs.empty() && m_durationsMs[0] <= 0)
        m_durationsMs[0] = 1;

    DebugLog::log("[sidebar-anim] loaded %d/%d frames (%dx%d -> %dx%d) from %s",
                  static_cast<int>(m_frames.size()), info.frame_count,
                  info.canvas_width, info.canvas_height, uploadW, uploadH,
                  webpPath.c_str());
    return true;
}

void SidebarAnimation::update(float dt, bool focused) {
    if (!focused || m_frames.empty()) {
        m_frameIndex  = 0;
        m_elapsedMs   = 0.f;
        return;
    }

    m_elapsedMs += dt * 1000.f;
    while (m_elapsedMs >= static_cast<float>(m_durationsMs[m_frameIndex])) {
        m_elapsedMs -= static_cast<float>(m_durationsMs[m_frameIndex]);
        m_frameIndex = (m_frameIndex + 1) % static_cast<int>(m_frames.size());
    }
}

nxui::Texture* SidebarAnimation::currentFrame() {
    if (m_frames.empty()) return nullptr;
    return &m_frames[m_frameIndex];
}

void SidebarAnimation::reset() {
    m_frameIndex = 0;
    m_elapsedMs  = 0.f;
}
