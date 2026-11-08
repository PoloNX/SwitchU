#include <nxui/core/SharedFonts.hpp>
#include <nxui/core/Font.hpp>

#include <switch.h>

#include <cstdio>
#include <vector>

namespace nxui::shared_fonts {
namespace {

struct Face {
    PlSharedFontType type = PlSharedFontType_Standard;
    const void* data = nullptr;
    std::size_t size = 0;
};

bool g_attempted = false;
bool g_ok = false;
std::vector<Face> g_faces;

bool mapFace(PlSharedFontType type) {
    PlFontData font{};
    const Result rc = plGetSharedFontByType(&font, type);
    if (R_FAILED(rc) || !font.address || font.size == 0) {
        std::fprintf(stderr,
                     "[shared-fonts] plGetSharedFontByType(%u) rc=0x%X\n",
                     static_cast<unsigned>(type), rc);
        return false;
    }
    g_faces.push_back(Face{type, font.address, static_cast<std::size_t>(font.size)});
    return true;
}

} // namespace

bool ensureLoaded() {
    if (g_attempted)
        return g_ok;
    g_attempted = true;
    g_faces.clear();

    // Order: Standard (JP + Latin) first, then Simplified / Ext / Traditional
    // Chinese, then Korean. Font fallback walks this list for missing glyphs.
    static const PlSharedFontType kTypes[] = {
        PlSharedFontType_Standard,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional,
        PlSharedFontType_KO,
    };
    for (PlSharedFontType type : kTypes)
        mapFace(type);

    g_ok = !g_faces.empty();
    std::fprintf(stderr, "[shared-fonts] loaded %zu face(s)\n", g_faces.size());
    return g_ok;
}

std::size_t faceCount() {
    ensureLoaded();
    return g_faces.size();
}

bool loadUiFont(Font& font, GpuDevice& gpu, Renderer& ren, int ptSize) {
    if (!ensureLoaded() || g_faces.empty())
        return false;

    const Face& primary = g_faces.front();
    if (!font.loadFromMemory(gpu, ren, primary.data, primary.size, ptSize))
        return false;

    for (std::size_t i = 1; i < g_faces.size(); ++i)
        font.addFallbackFromMemory(g_faces[i].data, g_faces[i].size);
    return true;
}

bool attachFallbacks(Font& font) {
    if (!font.valid())
        return false;
    if (!ensureLoaded() || g_faces.empty())
        return false;

    // Theme/custom primary already set — attach every shared face as fallback
    // (including Standard) so Japanese/Chinese/Korean titles still resolve.
    std::size_t attached = 0;
    for (const Face& face : g_faces) {
        if (font.addFallbackFromMemory(face.data, face.size))
            ++attached;
    }
    return attached > 0;
}

} // namespace nxui::shared_fonts
