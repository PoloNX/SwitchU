#include "SidebarAnimation.hpp"
#include "core/DebugLog.hpp"
#include <nxui/third_party/stb/stb_image.h>
#include <webp/demux.h>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cctype>

bool SidebarAnimation::load(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                            const std::string& webpPath,
                            int maximumSide,
                            std::size_t maximumGpuBytes) {
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

    std::string lowerPath = webpPath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (lowerPath.ends_with(".gif")) {
        int* delays = nullptr;
        int width = 0;
        int height = 0;
        int frameCount = 0;
        int channels = 0;
        stbi_uc* decoded = stbi_load_gif_from_memory(
            bytes.data(), static_cast<int>(bytes.size()), &delays,
            &width, &height, &frameCount, &channels, 4);
        if (!decoded || width <= 0 || height <= 0 || frameCount <= 0) {
            if (decoded) stbi_image_free(decoded);
            if (delays) stbi_image_free(delays);
            return false;
        }

        const int uploadSideLimit = maximumSide > 0 ? maximumSide : 384;
        int uploadWidth = width;
        int uploadHeight = height;
        if (uploadWidth > uploadSideLimit || uploadHeight > uploadSideLimit) {
            const float scale = std::min(
                static_cast<float>(uploadSideLimit) / uploadWidth,
                static_cast<float>(uploadSideLimit) / uploadHeight);
            uploadWidth = std::max(1, static_cast<int>(std::round(uploadWidth * scale)));
            uploadHeight = std::max(1, static_cast<int>(std::round(uploadHeight * scale)));
        }
        const std::size_t uploadBudget = maximumGpuBytes > 0
            ? maximumGpuBytes : 12u * 1024u * 1024u;
        const std::size_t bytesPerFrame = static_cast<std::size_t>(uploadWidth) *
            uploadHeight * 4u;
        const int framesToUpload = std::min(frameCount, static_cast<int>(
            std::max<std::size_t>(1u, uploadBudget /
                std::max<std::size_t>(1u, bytesPerFrame))));
        std::vector<std::uint8_t> scaled(bytesPerFrame);
        m_frames.reserve(static_cast<std::size_t>(framesToUpload));
        m_durationsMs.reserve(static_cast<std::size_t>(framesToUpload));
        for (int frame = 0; frame < framesToUpload; ++frame) {
            const int sourceFrame = frame * frameCount / framesToUpload;
            const int nextSourceFrame = (frame + 1) * frameCount / framesToUpload;
            const stbi_uc* source = decoded +
                static_cast<std::size_t>(sourceFrame) * width * height * 4u;
            const std::uint8_t* upload = source;
            if (uploadWidth != width || uploadHeight != height) {
                for (int y = 0; y < uploadHeight; ++y) {
                    const int sourceY = y * height / uploadHeight;
                    for (int x = 0; x < uploadWidth; ++x) {
                        const int sourceX = x * width / uploadWidth;
                        const auto* pixel = source +
                            (static_cast<std::size_t>(sourceY) * width + sourceX) * 4u;
                        auto* target = scaled.data() +
                            (static_cast<std::size_t>(y) * uploadWidth + x) * 4u;
                        std::copy_n(pixel, 4, target);
                    }
                }
                upload = scaled.data();
            }
            nxui::Texture texture;
            if (!texture.loadFromPixels(
                    gpu, ren, upload, uploadWidth, uploadHeight))
                break;
            m_frames.push_back(std::move(texture));
            int sampledDuration = 0;
            for (int sourceIndex = sourceFrame;
                 sourceIndex < nextSourceFrame; ++sourceIndex)
                sampledDuration += delays ? delays[sourceIndex] : 100;
            m_durationsMs.push_back(std::max(20, sampledDuration));
        }
        stbi_image_free(decoded);
        if (delays) stbi_image_free(delays);
        DebugLog::log("[sidebar-anim] loaded GIF frames=%d/%d size=%dx%d from %s",
                      static_cast<int>(m_frames.size()), frameCount,
                      uploadWidth, uploadHeight, webpPath.c_str());
        return !m_frames.empty();
    }

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

    const int uploadSideLimit = maximumSide > 0 ? maximumSide : 64;
    int uploadW = static_cast<int>(info.canvas_width);
    int uploadH = static_cast<int>(info.canvas_height);
    bool needScale = (uploadW > uploadSideLimit || uploadH > uploadSideLimit);
    if (needScale) {
        float scale = std::min(static_cast<float>(uploadSideLimit) / uploadW,
                               static_cast<float>(uploadSideLimit) / uploadH);
        uploadW = std::max(1, static_cast<int>(std::round(uploadW * scale)));
        uploadH = std::max(1, static_cast<int>(std::round(uploadH * scale)));
    }

    std::vector<uint8_t> scaledBuf;
    if (needScale)
        scaledBuf.resize(static_cast<size_t>(uploadW) * uploadH * 4u);

    const size_t uploadBudget = maximumGpuBytes > 0
        ? maximumGpuBytes : 2u * 1024u * 1024u;
    size_t bytesPerFrame = static_cast<size_t>(uploadW) * uploadH * 4u;
    size_t maxFrames = std::max<size_t>(1u, uploadBudget /
        std::max<size_t>(1u, bytesPerFrame));
    size_t reserve   = std::min<size_t>(maxFrames, std::max<size_t>(1u, info.frame_count));
    m_frames.reserve(reserve);
    m_durationsMs.reserve(reserve);

    int prevTs = 0;
    std::size_t decodedIndex = 0;
    std::size_t selectedFrames = 0;
    while (WebPAnimDecoderHasMoreFrames(dec)) {
        uint8_t* rgba = nullptr;
        int timestamp  = 0;
        if (!WebPAnimDecoderGetNext(dec, &rgba, &timestamp) || !rgba)
            break;

        int durationMs = timestamp - prevTs;
        if (durationMs <= 0) durationMs = 1;
        prevTs = timestamp;
        const std::size_t selectedIndex = selectedFrames < reserve
            ? selectedFrames * static_cast<std::size_t>(info.frame_count) / reserve
            : static_cast<std::size_t>(info.frame_count);
        const bool uploadFrame = decodedIndex == selectedIndex;
        ++decodedIndex;
        if (!uploadFrame) {
            if (!m_durationsMs.empty())
                m_durationsMs.back() += durationMs;
            continue;
        }

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
        if (tex.loadFromPixels(gpu, ren, uploadPixels, uploadW, uploadH)) {
            m_frames.push_back(std::move(tex));
            m_durationsMs.push_back(durationMs);
            ++selectedFrames;
        } else {
            DebugLog::log("[sidebar-anim] frame upload failed at %d",
                          static_cast<int>(m_frames.size()));
            break;
        }
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

void SidebarAnimation::clear() {
    m_frames.clear();
    m_frames.shrink_to_fit();
    m_durationsMs.clear();
    m_durationsMs.shrink_to_fit();
    m_frameIndex = 0;
    m_elapsedMs = 0.f;
}

bool SidebarAnimation::appendFrame(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                   const std::uint8_t* rgba,
                                   int width, int height, int durationMs) {
    if (!rgba || width <= 0 || height <= 0) return false;
    nxui::Texture texture;
    if (!texture.loadFromPixels(gpu, ren, rgba, width, height))
        return false;
    m_frames.push_back(std::move(texture));
    m_durationsMs.push_back(std::max(1, durationMs));
    return true;
}
