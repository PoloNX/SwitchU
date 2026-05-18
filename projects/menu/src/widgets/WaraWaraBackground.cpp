#include "WaraWaraBackground.hpp"
#include "core/DebugLog.hpp"
#include <cmath>
#include <cstdlib>

namespace {

constexpr int kBackgroundRenderReserveVertices = 13312;
constexpr int kWorstCaseShapeVertices = 36;
constexpr int kGlassShapeLayers = 3;
constexpr int kGridRenderedShapeBudget = 448;
constexpr int kFloatingRenderedShapeBudget = 160;
constexpr int kDenseRenderCopyThreshold = 96;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;

float degreesToRadians(float degrees) {
    return degrees * kPi / 180.f;
}

void appendArcPoints(std::vector<nxui::Vec2>& points,
                     float centerX,
                     float centerY,
                     float radius,
                     float startAngle,
                     float endAngle,
                     int segments,
                     bool includeStart)
{
    int startIndex = includeStart ? 0 : 1;
    for (int i = startIndex; i <= segments; ++i) {
        float t = segments > 0 ? (float)i / (float)segments : 0.f;
        float angle = startAngle + (endAngle - startAngle) * t;
        points.push_back({centerX + std::cos(angle) * radius,
                          centerY + std::sin(angle) * radius});
    }
}

float random01() {
    return (std::rand() % 1000) / 1000.f;
}

float randomRange(float minValue, float maxValue) {
    return minValue + (maxValue - minValue) * random01();
}

float wrapValue(float value, float minValue, float maxValue) {
    float span = maxValue - minValue;
    if (span <= 0.f)
        return minValue;
    while (value < minValue)
        value += span;
    while (value > maxValue)
        value -= span;
    return value;
}

int symmetryMultiplier(WaraWaraBackground::Symmetry symmetry) {
    switch (symmetry) {
        case WaraWaraBackground::Symmetry::MirrorHorizontal:
        case WaraWaraBackground::Symmetry::MirrorVertical:
            return 2;
        case WaraWaraBackground::Symmetry::Quad:
            return 4;
        case WaraWaraBackground::Symmetry::None:
        default:
            return 1;
    }
}

int maxSafeBaseShapeCount(const WaraWaraBackground::Config& config) {
    const int multiplier = std::max(1, symmetryMultiplier(config.symmetry));
    const int maxBudget = std::max(1, nxui::GpuDevice::MAX_VERTICES - kBackgroundRenderReserveVertices);
    const int perShapeBudget = std::max(1, kWorstCaseShapeVertices * kGlassShapeLayers * multiplier);
    return std::max(1, maxBudget / perShapeBudget);
}

int maxVisualBaseShapeCount(const WaraWaraBackground::Config& config) {
    const int multiplier = std::max(1, symmetryMultiplier(config.symmetry));
    const int renderedBudget = (config.layout == WaraWaraBackground::Layout::Grid)
        ? kGridRenderedShapeBudget
        : kFloatingRenderedShapeBudget;
    return std::max(1, renderedBudget / multiplier);
}

int renderedShapeCopies(const WaraWaraBackground::Config& config, int baseShapeCount) {
    return std::max(0, baseShapeCount) * std::max(1, symmetryMultiplier(config.symmetry));
}

bool useDenseRenderMode(const WaraWaraBackground::Config& config, int baseShapeCount) {
    return config.layout == WaraWaraBackground::Layout::Grid
        && renderedShapeCopies(config, baseShapeCount) >= kDenseRenderCopyThreshold;
}

std::uint64_t estimateVertexCount(const WaraWaraBackground::Config& config, int baseShapeCount) {
    return (std::uint64_t)std::max(0, baseShapeCount)
        * (std::uint64_t)std::max(1, symmetryMultiplier(config.symmetry))
        * (std::uint64_t)kWorstCaseShapeVertices
        * (std::uint64_t)kGlassShapeLayers;
}

} // namespace

WaraWaraBackground::WaraWaraBackground() { regenerate(30); }

void WaraWaraBackground::setConfig(const Config& config) {
    m_config = config;
    m_config.shapeCount = std::max(1, m_config.shapeCount);
    m_config.gridColumns = std::max(1, m_config.gridColumns);
    m_config.gridRows = std::max(1, m_config.gridRows);
    m_config.spacingX = std::max(1.f, m_config.spacingX);
    m_config.spacingY = std::max(1.f, m_config.spacingY);
    m_config.sizeMin = std::max(1.f, m_config.sizeMin);
    m_config.sizeMax = std::max(m_config.sizeMin, m_config.sizeMax);
    m_config.speedMin = std::max(0.f, m_config.speedMin);
    m_config.speedMax = std::max(m_config.speedMin, m_config.speedMax);
    m_config.wobble = std::max(0.f, m_config.wobble);
    m_config.opacity = std::clamp(m_config.opacity, 0.f, 1.f);
    m_config.cornerRoundness = std::clamp(m_config.cornerRoundness, 0.f, 1.f);
    m_config.imageOpacity = std::clamp(m_config.imageOpacity, 0.f, 1.f);
    regenerate(0);
}

bool WaraWaraBackground::loadImage(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path) {
    if (path.empty()) {
        clearImage();
        return false;
    }

    nxui::Texture texture;
    if (!texture.loadFromFile(gpu, ren, path, 1280))
        return false;

    m_backgroundImage = std::move(texture);
    return true;
}

void WaraWaraBackground::clearImage() {
    m_backgroundImage = nxui::Texture{};
}

WaraWaraBackground::ShapeType WaraWaraBackground::pickShapeType() const {
    switch (m_config.shapeSet) {
        case ShapeSet::Circle:
            return Circle;
        case ShapeSet::Triangle:
            return Triangle;
        case ShapeSet::Square:
            return Square;
        case ShapeSet::Diamond:
            return Diamond;
        case ShapeSet::Hexagon:
            return Hexagon;
        case ShapeSet::Mixed:
        default:
            return static_cast<ShapeType>(std::rand() % ShapeCount);
    }
}

void WaraWaraBackground::regenerate(int count) {
    float areaX = m_rect.x;
    float areaY = m_rect.y;
    float areaW = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
    float areaH = (m_rect.height > 1.f) ? m_rect.height : 720.f;

    int requestedShapeCount = count > 0 ? count : m_config.shapeCount;
    const int requestedGridCells = std::max(1, m_config.gridColumns * m_config.gridRows);
    if (m_config.layout == Layout::Grid)
        requestedShapeCount = requestedGridCells;

    const int safeShapeCount = maxSafeBaseShapeCount(m_config);
    const int visualShapeCount = maxVisualBaseShapeCount(m_config);
    const int shapeCount = std::min(requestedShapeCount, std::min(safeShapeCount, visualShapeCount));
    if (shapeCount != requestedShapeCount) {
        DebugLog::log("[background] shape request clamped: layout=%s requested=%d effective=%d symmetry=%d estVerts=%llu safeVerts=%d visualCap=%d renderedCopies=%d",
                      m_config.layout == Layout::Grid ? "grid" : "floating",
                      requestedShapeCount,
                      shapeCount,
                      symmetryMultiplier(m_config.symmetry),
                      (unsigned long long)estimateVertexCount(m_config, requestedShapeCount),
                      nxui::GpuDevice::MAX_VERTICES - kBackgroundRenderReserveVertices,
                      visualShapeCount,
                      renderedShapeCopies(m_config, shapeCount));
    }

    m_shapes.resize(shapeCount);
    for (int index = 0; index < shapeCount; ++index) {
        Shape& s = m_shapes[index];
        s.type = pickShapeType();
        if (m_config.layout == Layout::Grid) {
            const int cellIndex = std::min(requestedGridCells - 1,
                                           (shapeCount >= requestedGridCells)
                                               ? index
                                               : (int)(((long long)index * requestedGridCells) / shapeCount));
            int col = cellIndex % m_config.gridColumns;
            int row = cellIndex / m_config.gridColumns;
            float gridW = (m_config.gridColumns - 1) * m_config.spacingX;
            float gridH = (m_config.gridRows - 1) * m_config.spacingY;
            float startX = areaX + (areaW - gridW) * 0.5f;
            float startY = areaY + (areaH - gridH) * 0.5f;
            s.pos = {startX + col * m_config.spacingX, startY + row * m_config.spacingY};
        } else {
            s.pos = {areaX + random01() * areaW, areaY + random01() * areaH};
        }
        s.size = randomRange(m_config.sizeMin, m_config.sizeMax);
        s.speed = randomRange(m_config.speedMin, m_config.speedMax);
        s.phase = random01() * 6.28f;
        s.wobble = m_config.layout == Layout::Grid ? 0.f : randomRange(m_config.wobble * 0.4f, m_config.wobble);
        if (m_config.fixedOrientation) {
            s.rotation = degreesToRadians(m_config.orientationDegrees);
            s.rotSpeed = m_config.rotationSpeed;
        } else {
            s.rotation = random01() * 6.28f;
            s.rotSpeed = randomRange(m_config.rotationSpeed * 0.35f, m_config.rotationSpeed);
            if (std::rand() % 2) s.rotSpeed = -s.rotSpeed;
        }
        s.glassAlpha = randomRange(0.05f, 0.16f) * m_config.opacity;
        s.color = nxui::Color::white().withAlpha(s.glassAlpha);
    }
}

void WaraWaraBackground::onUpdate(float dt) {
    m_time += dt;
    for (auto& s : m_shapes) {
        if (m_config.layout == Layout::Floating) {
            float top = m_rect.y - s.size - 20.f;
            float bottom = m_rect.y + ((m_rect.height > 1.f) ? m_rect.height : 720.f) + s.size + 20.f;
            float left = m_rect.x;
            float width = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
            s.pos.y -= s.speed * dt;
            s.pos.x += std::sin(m_time * 0.7f + s.phase) * s.wobble * dt;
            s.pos.x = wrapValue(s.pos.x, left - s.size, left + width + s.size);
            if (s.pos.y + s.size < top) {
                s.pos.y = bottom;
                s.pos.x = left + random01() * width;
            }
        }
        s.rotation += s.rotSpeed * dt;
    }
}

nxui::Rect WaraWaraBackground::backgroundImageRect() const {
    if (!m_backgroundImage.valid() || m_backgroundImage.width() <= 0 || m_backgroundImage.height() <= 0)
        return m_rect;

    float areaX = m_rect.x;
    float areaY = m_rect.y;
    float areaW = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
    float areaH = (m_rect.height > 1.f) ? m_rect.height : 720.f;
    float texW = static_cast<float>(m_backgroundImage.width());
    float texH = static_cast<float>(m_backgroundImage.height());
    float sx = areaW / texW;
    float sy = areaH / texH;
    float scale = m_config.imageCover ? std::max(sx, sy) : std::min(sx, sy);
    float drawW = texW * scale;
    float drawH = texH * scale;
    return {areaX + (areaW - drawW) * 0.5f, areaY + (areaH - drawH) * 0.5f, drawW, drawH};
}

void WaraWaraBackground::onRender(nxui::Renderer& ren) {
    ren.useShader(nxui::ShaderProgram::Gradient);
    nxui::FsUniforms fs = {};
    fs.useTexture = 0;
    fs.param1 = m_time;
    fs.extra[0] = m_accent.r;  fs.extra[1] = m_accent.g;
    fs.extra[2] = m_accent.b;  fs.extra[3] = m_accent.a;
    fs.extra[4] = m_secondary.r;  fs.extra[5] = m_secondary.g;
    fs.extra[6] = m_secondary.b;  fs.extra[7] = m_secondary.a;
    fs.extra[8]  = m_shapeColor.r * 2.f;  fs.extra[9]  = m_shapeColor.g * 2.f;
    fs.extra[10] = m_shapeColor.b * 2.f;  fs.extra[11] = m_shapeColor.a;
    ren.pushFsUniforms(fs);
    ren.drawRect(m_rect, nxui::Color::white());
    ren.flush();
    ren.useShader(nxui::ShaderProgram::Basic);

    if (m_backgroundImage.valid() && m_config.imageOpacity > 0.f) {
        ren.drawTexture(&m_backgroundImage,
                        backgroundImageRect(),
                        nxui::Color::white().withAlpha(m_config.imageOpacity * m_opacity));
    }

    for (const auto& s : m_shapes)
        drawShapeWithSymmetry(ren, s);

    ren.flush();
}

void WaraWaraBackground::drawShapeWithSymmetry(nxui::Renderer& ren, const Shape& s) const {
    drawGlassShape(ren, s);

    float left = m_rect.x;
    float top = m_rect.y;
    float width = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
    float height = (m_rect.height > 1.f) ? m_rect.height : 720.f;

    auto mirrorHorizontal = [&](const Shape& source) {
        Shape mirrored = source;
        mirrored.pos.x = left + width - (source.pos.x - left);
        return mirrored;
    };

    auto mirrorVertical = [&](const Shape& source) {
        Shape mirrored = source;
        mirrored.pos.y = top + height - (source.pos.y - top);
        return mirrored;
    };

    if (m_config.symmetry == Symmetry::MirrorHorizontal || m_config.symmetry == Symmetry::Quad)
        drawGlassShape(ren, mirrorHorizontal(s));

    if (m_config.symmetry == Symmetry::MirrorVertical || m_config.symmetry == Symmetry::Quad)
        drawGlassShape(ren, mirrorVertical(s));

    if (m_config.symmetry == Symmetry::Quad)
        drawGlassShape(ren, mirrorVertical(mirrorHorizontal(s)));
}

void WaraWaraBackground::drawGlassShape(nxui::Renderer& ren, const Shape& s) const {
    float a = s.color.a * m_opacity;
    if (a < 0.003f) return;

    nxui::Color body = m_shapeColor.withAlpha(a);
    drawRoundedShape(ren, s, body);

    if (useDenseRenderMode(m_config, (int)m_shapes.size()))
        return;

    Shape highlight = s;
    highlight.pos.y -= s.size * 0.08f;
    highlight.size   = s.size * 0.85f;
    nxui::Color hi = nxui::Color(1.f, 1.f, 1.f, a * 0.35f);
    drawRoundedShape(ren, highlight, hi);

    Shape edge = s;
    edge.size = s.size * 1.06f;
    nxui::Color edgeC = nxui::Color(1.f, 1.f, 1.f, a * 0.15f);
    drawRoundedShape(ren, edge, edgeC);
}

void WaraWaraBackground::drawRoundedShape(nxui::Renderer& ren, const Shape& s, const nxui::Color& c) const {
    float r  = s.rotation;
    float sz = s.size;

    auto rot = [&](float lx, float ly) -> nxui::Vec2 {
        float cs = std::cos(r), sn = std::sin(r);
        return {s.pos.x + lx * cs - ly * sn,
                s.pos.y + lx * sn + ly * cs};
    };

    switch (s.type) {
    case Circle:
        ren.drawCircle(s.pos, sz, c, 12);
        break;
    case Triangle: {
        nxui::Vec2 p0 = rot(0,             -sz);
        nxui::Vec2 p1 = rot(-sz * 0.866f,   sz * 0.5f);
        nxui::Vec2 p2 = rot( sz * 0.866f,   sz * 0.5f);
        ren.drawTriangle(p0, p1, p2, c);
        break;
    }
    case Square: {
        float h = sz * 0.707f;
        float roundness = std::clamp(m_config.cornerRoundness, 0.f, 1.f);
        if (roundness > 0.001f) {
            float radius = h * roundness;
            std::vector<nxui::Vec2> points;
            points.reserve(16);
            appendArcPoints(points,  h - radius, -h + radius, radius, -kHalfPi, 0.f,      3, true);
            appendArcPoints(points,  h - radius,  h - radius, radius,  0.f,      kHalfPi,  3, false);
            appendArcPoints(points, -h + radius,  h - radius, radius,  kHalfPi,  kPi,      3, false);
            appendArcPoints(points, -h + radius, -h + radius, radius,  kPi,      kPi * 1.5f, 3, false);

            for (size_t i = 0; i < points.size(); ++i)
                points[i] = rot(points[i].x, points[i].y);

            for (size_t i = 0; i < points.size(); ++i)
                ren.drawTriangle(s.pos, points[i], points[(i + 1) % points.size()], c);
            break;
        }

        nxui::Vec2 p0 = rot(-h, -h);
        nxui::Vec2 p1 = rot( h, -h);
        nxui::Vec2 p2 = rot( h,  h);
        nxui::Vec2 p3 = rot(-h,  h);
        ren.drawTriangle(p0, p1, p2, c);
        ren.drawTriangle(p0, p2, p3, c);
        break;
    }
    case Diamond: {
        nxui::Vec2 p0 = rot(0,            -sz);
        nxui::Vec2 p1 = rot( sz * 0.6f,    0);
        nxui::Vec2 p2 = rot(0,             sz);
        nxui::Vec2 p3 = rot(-sz * 0.6f,    0);
        ren.drawTriangle(p0, p1, p2, c);
        ren.drawTriangle(p0, p2, p3, c);
        break;
    }
    case Hexagon: {
        constexpr int N = 6;
        const float step = 6.28318f / N;
        nxui::Vec2 pts[N];
        for (int i = 0; i < N; ++i) {
            float a2 = step * i;
            pts[i] = rot(std::cos(a2) * sz, std::sin(a2) * sz);
        }
        for (int i = 1; i < N - 1; ++i)
            ren.drawTriangle(pts[0], pts[i], pts[i + 1], c);
        break;
    }
    default: break;
    }
}
