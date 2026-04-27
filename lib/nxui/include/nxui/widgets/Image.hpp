#pragma once
#include <nxui/widgets/Box.hpp>
#include <nxui/core/Texture.hpp>
#include <string>

namespace nxui {

class GpuDevice;
class Renderer;

/// Displays a texture. Can load from file path, memory, or be set directly.
class Image : public Box {
public:
    Image() = default;

    // Texture source

    /// Set an externally managed texture (Image does NOT own it).
    void setTexture(Texture* tex) { m_externalTex = tex; }

    /// Load from file on filesystem (romfs:/ or sdmc:/).
    bool loadFromFile(GpuDevice& gpu, Renderer& ren, const std::string& path);

    /// Load from raw RGBA pixels.
    bool loadFromPixels(GpuDevice& gpu, Renderer& ren,
                        const uint8_t* rgba, int w, int h);

    /// Load from encoded image in memory (PNG/JPEG).
    bool loadFromMemory(GpuDevice& gpu, Renderer& ren,
                        const void* data, size_t size);

    /// Get the underlying texture (owned or external).
    const Texture* texture() const;

    // Display options

    void setTint(const Color& c)       { m_tint = c; }
    const Color& tint() const          { return m_tint; }

    void setCornerRadius(float r)      { m_cornerRadius = r; }
    float cornerRadius() const         { return m_cornerRadius; }

    /// Resize widget to match texture dimensions.
    void sizeToTexture();

    /// Scale mode: how the image fits within the widget rect.
    enum class ScaleMode { Stretch, Fit, Fill };
    void setScaleMode(ScaleMode m) { m_scaleMode = m; }

protected:
    void onRender(Renderer& ren) override;

private:
    Rect computeDrawRect() const;

    Texture* m_externalTex = nullptr;  // non-owning
    Texture  m_ownedTex;               // owning (for loadFrom*)
    Color    m_tint = Color::white();
    float    m_cornerRadius = 0.f;
    ScaleMode m_scaleMode = ScaleMode::Stretch;
};

} // namespace nxui
