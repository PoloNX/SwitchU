#pragma once
#include <nxui/widgets/Background.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>
#include <vector>


class WaraWaraBackground : public nxui::Background {
public:
    enum class Layout {
        Floating,
        Grid,
    };

    enum class ShapeSet {
        Mixed,
        Circle,
        Triangle,
        Square,
        Diamond,
        Hexagon,
    };

    enum class Symmetry {
        None,
        MirrorHorizontal,
        MirrorVertical,
        Quad,
    };

    struct Config {
        Layout layout = Layout::Floating;
        ShapeSet shapeSet = ShapeSet::Mixed;
        Symmetry symmetry = Symmetry::None;
        int shapeCount = 30;
        int gridColumns = 14;
        int gridRows = 8;
        float spacingX = 88.f;
        float spacingY = 88.f;
        float sizeMin = 14.f;
        float sizeMax = 54.f;
        float speedMin = 6.f;
        float speedMax = 28.f;
        float wobble = 16.f;
        float opacity = 1.f;
        float rotationSpeed = 0.5f;
        bool fixedOrientation = false;
        float orientationDegrees = 0.f;
        float cornerRoundness = 0.f;
        float imageOpacity = 0.f;
        bool imageCover = true;
    };

    WaraWaraBackground();

    void setConfig(const Config& config);
    const Config& config() const { return m_config; }

    bool loadImage(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);
    void clearImage();

    void setMotionPaused(bool paused) { m_motionPaused = paused; }
    bool motionPaused() const { return m_motionPaused; }

    // Lite / Frame Tap: drop animated shapes only; theme wallpaper stays.
    void setLiteSuppressed(bool suppressed);
    bool liteSuppressed() const { return m_liteSuppressed; }

    /// Shapes (+ optional theme image) without the opaque gradient fill —
    /// for compositing over blurred letterbox regions.
    void renderLetterboxOverlay(nxui::Renderer& ren, float opacityMul = 1.f);

    /// Full wallpaper stack (gradient + image + shapes) shifted by dy —
    /// used as the Lock curtain plane so Theme Shop art slides with unlock.
    /// Optional uniform scale around screen center (Lock Zoom transition).
    void renderShifted(nxui::Renderer& ren, float dy, float opacityMul = 1.f,
                       float scale = 1.f);

    void regenerate(int count = 50) override;

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    enum ShapeType { Circle, Triangle, Square, Diamond, Hexagon, ShapeCount };

    struct Shape {
        ShapeType type;
        nxui::Vec2  pos;
        float size;
        float speed;
        float phase;
        float wobble;
        float rotation;
        float rotSpeed;
        nxui::Color color;
        float glassAlpha;
    };

    ShapeType pickShapeType() const;
    nxui::Rect backgroundImageRect() const;
    void drawShapeWithSymmetry(nxui::Renderer& ren, const Shape& s) const;
    void drawGlassShape(nxui::Renderer& ren, const Shape& s) const;
    void drawRoundedShape(nxui::Renderer& ren, const Shape& s, const nxui::Color& c) const;

    Config m_config;
    std::vector<Shape> m_shapes;
    nxui::Texture m_backgroundImage;
    float m_time = 0.f;
    bool m_motionPaused = false;
    bool m_liteSuppressed = false;
};

