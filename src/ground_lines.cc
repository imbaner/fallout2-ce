#include "ground_lines.h"

#include <algorithm>
#include <cmath>

#include "object.h"
#include "world_view.h"

namespace fallout {

namespace {

    // How much of a line shows over what covers the ground there
    // (`ObjectSeeThroughCover`).
    constexpr float kShownOverGround = 1.0f;
    constexpr float kShownThroughSeeThrough = 0.45f;
    constexpr float kShownBehindSolid = 0.4f;
    constexpr float kShownUnderTop = 0.0f;

    // Soft edge on each side, output pixels.
    constexpr float kFeather = 1.0f;

    // A corner's offset is at most this many half widths (sharp turns).
    constexpr float kMiterLimit = 2.0f;

    struct State {
        // World to output pixels: origin + world * zoom.
        float originX = 0.0f;
        float originY = 0.0f;
        float zoomX = 1.0f;
        float zoomY = 1.0f;

        const unsigned char* cover = nullptr;
        int coverWidth = 0;
        int coverHeight = 0;
        int coverPitch = 0;

        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;
    };

    State gState;

    // A cross-section of a stroke: where (world), how much of it shows, the
    // offset to its sides (output pixels, the half width included).
    struct Station {
        float x;
        float y;
        float shown;
        float sideX;
        float sideY;
    };

    float shownAt(float worldX, float worldY)
    {
        if (gState.cover == nullptr) {
            return kShownOverGround;
        }

        int x = static_cast<int>(std::floor(worldX));
        int y = static_cast<int>(std::floor(worldY));
        if (x < 0 || y < 0 || x >= gState.coverWidth || y >= gState.coverHeight) {
            return kShownOverGround;
        }

        switch (static_cast<ObjectSeeThroughCover>(gState.cover[gState.coverPitch * y + x])) {
        case ObjectSeeThroughCover::SeeThrough:
            return kShownThroughSeeThrough;
        case ObjectSeeThroughCover::Solid:
            return kShownBehindSolid;
        case ObjectSeeThroughCover::Top:
            return kShownUnderTop;
        default:
            return kShownOverGround;
        }
    }

    // Four vertices across: soft edge, the line, the line, soft edge.
    void addCrossSection(const Station& station, float halfWidth, MuiColor color)
    {
        float x = gState.originX + station.x * gState.zoomX;
        float y = gState.originY + station.y * gState.zoomY;
        float outer = (halfWidth + kFeather) / halfWidth;
        Uint8 alpha = static_cast<Uint8>(std::lround(color.a * station.shown));
        SDL_Color solid = { color.r, color.g, color.b, alpha };
        SDL_Color clear = { color.r, color.g, color.b, 0 };
        gState.vertices.push_back({ { x - station.sideX * outer, y - station.sideY * outer }, clear, { 0.0f, 0.0f } });
        gState.vertices.push_back({ { x - station.sideX, y - station.sideY }, solid, { 0.0f, 0.0f } });
        gState.vertices.push_back({ { x + station.sideX, y + station.sideY }, solid, { 0.0f, 0.0f } });
        gState.vertices.push_back({ { x + station.sideX * outer, y + station.sideY * outer }, clear, { 0.0f, 0.0f } });
    }

    // Three quads between two cross-sections.
    void connect(int from, int to)
    {
        for (int band = 0; band < 3; band++) {
            int a = from + band;
            int b = to + band;
            gState.indices.insert(gState.indices.end(), { a, a + 1, b + 1, a, b + 1, b });
        }
    }

} // namespace

void groundLinesBegin(float scale)
{
    float originX;
    float originY;
    float unitX;
    float unitY;
    worldViewWorldToScreenF(0.0f, 0.0f, &originX, &originY);
    worldViewWorldToScreenF(1.0f, 1.0f, &unitX, &unitY);
    gState.originX = originX * scale;
    gState.originY = originY * scale;
    gState.zoomX = (unitX - originX) * scale;
    gState.zoomY = (unitY - originY) * scale;

    gState.cover = objectSeeThroughCover(&gState.coverWidth, &gState.coverHeight, &gState.coverPitch);

    gState.vertices.clear();
    gState.indices.clear();
}

void groundLinesAddPath(const std::vector<SDL_FPoint>& points, bool closed, float width, MuiColor color)
{
    int count = static_cast<int>(points.size());
    int segments = closed ? count : count - 1;
    if (count < 2 || width <= 0.0f) {
        return;
    }

    float halfWidth = width / 2.0f;

    // Each segment's unit normal on the screen.
    std::vector<SDL_FPoint> normals(segments);
    for (int segment = 0; segment < segments; segment++) {
        const SDL_FPoint& from = points[segment];
        const SDL_FPoint& to = points[(segment + 1) % count];
        float dx = (to.x - from.x) * gState.zoomX;
        float dy = (to.y - from.y) * gState.zoomY;
        float length = std::sqrt(dx * dx + dy * dy);
        normals[segment] = length > 0.0f ? SDL_FPoint { -dy / length, dx / length } : SDL_FPoint { 0.0f, 0.0f };
    }

    // A corner's side offset: along the two normals' middle, long enough
    // for the sides to stay parallel to both segments (mitered).
    auto cornerStation = [&](int point) {
        const SDL_FPoint& position = points[point];
        SDL_FPoint normal;
        if (!closed && point == 0) {
            normal = normals[0];
        } else if (!closed && point == count - 1) {
            normal = normals[segments - 1];
        } else {
            const SDL_FPoint& before = normals[(point + segments - 1) % segments];
            const SDL_FPoint& after = normals[point % segments];
            float x = before.x + after.x;
            float y = before.y + after.y;
            float length = std::sqrt(x * x + y * y);
            if (length < 0.001f) {
                normal = after;
            } else {
                x /= length;
                y /= length;
                float scale = std::min(1.0f / std::max(x * after.x + y * after.y, 0.001f), kMiterLimit);
                normal = { x * scale, y * scale };
            }
        }
        return Station { position.x, position.y, shownAt(position.x, position.y), normal.x * halfWidth, normal.y * halfWidth };
    };

    int first = static_cast<int>(gState.vertices.size());
    Station start = cornerStation(0);
    addCrossSection(start, halfWidth, color);
    int previous = first;

    for (int segment = 0; segment < segments; segment++) {
        const SDL_FPoint& from = points[segment];
        int endPoint = (segment + 1) % count;
        const SDL_FPoint& to = points[endPoint];
        const SDL_FPoint& normal = normals[segment];

        // Where what covers the ground changes along the segment (a world
        // pixel apart), the line changes from one side to the other.
        float dx = to.x - from.x;
        float dy = to.y - from.y;
        int steps = std::max(static_cast<int>(std::ceil(std::sqrt(dx * dx + dy * dy))), 1);
        float shown = shownAt(from.x, from.y);
        for (int step = 1; step < steps; step++) {
            float x = from.x + dx * step / steps;
            float y = from.y + dy * step / steps;
            float next = shownAt(x, y);
            if (next == shown) {
                continue;
            }

            float lastX = from.x + dx * (step - 1) / steps;
            float lastY = from.y + dy * (step - 1) / steps;
            for (const Station& station : { Station { lastX, lastY, shown, normal.x * halfWidth, normal.y * halfWidth },
                     Station { x, y, next, normal.x * halfWidth, normal.y * halfWidth } }) {
                int index = static_cast<int>(gState.vertices.size());
                addCrossSection(station, halfWidth, color);
                connect(previous, index);
                previous = index;
            }
            shown = next;
        }

        if (closed && endPoint == 0) {
            connect(previous, first);
        } else {
            int index = static_cast<int>(gState.vertices.size());
            addCrossSection(cornerStation(endPoint), halfWidth, color);
            connect(previous, index);
            previous = index;
        }
    }
}

void groundLinesEnd()
{
    SDL_Renderer* renderer = muiDrawGetRenderer();
    if (renderer != nullptr && !gState.indices.empty()) {
        SDL_RenderGeometry(renderer, nullptr, gState.vertices.data(), static_cast<int>(gState.vertices.size()), gState.indices.data(), static_cast<int>(gState.indices.size()));
    }
    gState.vertices.clear();
    gState.indices.clear();
}

} // namespace fallout
