#include "mui_map_view.h"

#include <algorithm>
#include <cmath>

namespace fallout {

namespace {

    const MuiColor kScenery = muiRgb(0x2E7A3A);

    // Texture pixels per tile: steps the map is drawn at (the view scales
    // the nearest one), the texture size limit.
    const float kScales[] = { 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f };
    constexpr int kMaxTextureSize = 2048;

    // Explored part margin, tiles.
    constexpr int kMargin = 3;

    SDL_Vertex vertex(float x, float y, MuiColor color)
    {
        SDL_Vertex result;
        result.position = { x, y };
        result.color = { color.r, color.g, color.b, color.a };
        result.tex_coord = { 0.0f, 0.0f };
        return result;
    }

    void addQuad(std::vector<SDL_Vertex>& vertices, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, MuiColor color)
    {
        vertices.push_back(vertex(a.x, a.y, color));
        vertices.push_back(vertex(b.x, b.y, color));
        vertices.push_back(vertex(c.x, c.y, color));
        vertices.push_back(vertex(a.x, a.y, color));
        vertices.push_back(vertex(c.x, c.y, color));
        vertices.push_back(vertex(d.x, d.y, color));
    }

    void addSegment(std::vector<SDL_Vertex>& vertices, float x1, float y1, float x2, float y2, float thickness, MuiColor color)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f) {
            return;
        }

        float nx = -dy / length * thickness / 2.0f;
        float ny = dx / length * thickness / 2.0f;
        addQuad(vertices, { x1 + nx, y1 + ny }, { x2 + nx, y2 + ny }, { x2 - nx, y2 - ny }, { x1 - nx, y1 - ny }, color);
    }

    // Joint of segments (octagon).
    void addDot(std::vector<SDL_Vertex>& vertices, float x, float y, float radius, MuiColor color)
    {
        constexpr int kSides = 8;
        for (int side = 0; side < kSides; side++) {
            float a1 = side * 2.0f * static_cast<float>(M_PI) / kSides;
            float a2 = (side + 1) * 2.0f * static_cast<float>(M_PI) / kSides;
            vertices.push_back(vertex(x, y, color));
            vertices.push_back(vertex(x + std::cos(a1) * radius, y + std::sin(a1) * radius, color));
            vertices.push_back(vertex(x + std::cos(a2) * radius, y + std::sin(a2) * radius, color));
        }
    }

} // namespace

unsigned char MuiMapView::tileAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return MUI_MAP_TILE_EMPTY;
    }
    return tiles[static_cast<size_t>(y) * width + x];
}

void MuiMapView::setTiles(const std::string& newKey, const std::vector<unsigned char>& newTiles, int newWidth, int newHeight)
{
    bool anotherMap = newKey != key;
    if (!anotherMap && newTiles == tiles) {
        return;
    }

    key = newKey;
    tiles = newTiles;
    width = newWidth;
    height = newHeight;
    textureValid = false;

    left = width;
    top = height;
    right = -1;
    bottom = -1;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (tileAt(x, y) != MUI_MAP_TILE_EMPTY) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }

    if (right >= left) {
        left = std::max(left - kMargin, 0);
        top = std::max(top - kMargin, 0);
        right = std::min(right + kMargin, width - 1);
        bottom = std::min(bottom + kMargin, height - 1);
    }

    if (anotherMap) {
        needsFit = true;
    }
}

void MuiMapView::centerOn(float x, float y)
{
    hasCenter = true;
    centerX = x;
    centerY = y;
}

// Draws the explored part into the texture at [scale] texture pixels per
// tile: dim scenery dots, then walls as lines between neighbouring walls
// with a glow.
void MuiMapView::render(MuiContext& ui, float scale)
{
    SDL_Renderer* renderer = muiDrawGetRenderer();
    if (renderer == nullptr) {
        return;
    }

    int newWidth = static_cast<int>(std::ceil((right - left + 1) * scale));
    int newHeight = static_cast<int>(std::ceil((bottom - top + 1) * scale));

    if (texture != nullptr && (textureGeneration != muiDrawGeneration() || newWidth != textureWidth || newHeight != textureHeight)) {
        if (textureGeneration == muiDrawGeneration()) {
            SDL_DestroyTexture(texture);
        }
        texture = nullptr;
    }

    if (texture == nullptr) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, newWidth, newHeight);
        if (texture == nullptr) {
            return;
        }
        SDL_SetTextureBlendMode(texture, muiPremultipliedBlendMode());
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
        textureWidth = newWidth;
        textureHeight = newHeight;
        textureGeneration = muiDrawGeneration();
    }

    const MuiTheme& theme = muiTheme();
    MuiColor glow = theme.accent.withAlpha(60);
    MuiColor core = theme.accent;
    float thickness = std::max(scale * 0.34f, 1.5f);

    std::vector<SDL_Vertex> scenery;
    std::vector<SDL_Vertex> glowVertices;
    std::vector<SDL_Vertex> coreVertices;

    auto center = [&](int x, int y) {
        return SDL_FPoint { (x - left + 0.5f) * scale, (y - top + 0.5f) * scale };
    };

    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            unsigned char tile = tileAt(x, y);
            SDL_FPoint p = center(x, y);

            if (tile == MUI_MAP_TILE_SCENERY) {
                addDot(scenery, p.x, p.y, scale * 0.32f, kScenery.withAlpha(200));
                continue;
            }

            if (tile != MUI_MAP_TILE_WALL) {
                continue;
            }

            addDot(glowVertices, p.x, p.y, thickness * 1.6f, glow);
            addDot(coreVertices, p.x, p.y, thickness / 2.0f, core);

            // Lines to walls right, below and diagonal (diagonals only where
            // no straight line joins them already).
            bool rightWall = tileAt(x + 1, y) == MUI_MAP_TILE_WALL;
            bool belowWall = tileAt(x, y + 1) == MUI_MAP_TILE_WALL;
            bool leftWall = tileAt(x - 1, y) == MUI_MAP_TILE_WALL;

            auto link = [&](int toX, int toY) {
                SDL_FPoint q = center(toX, toY);
                addSegment(glowVertices, p.x, p.y, q.x, q.y, thickness * 3.2f, glow);
                addSegment(coreVertices, p.x, p.y, q.x, q.y, thickness, core);
            };

            if (rightWall) {
                link(x + 1, y);
            }
            if (belowWall) {
                link(x, y + 1);
            }
            if (tileAt(x + 1, y + 1) == MUI_MAP_TILE_WALL && !rightWall && !belowWall) {
                link(x + 1, y + 1);
            }
            if (tileAt(x - 1, y + 1) == MUI_MAP_TILE_WALL && !leftWall && !belowWall) {
                link(x - 1, y + 1);
            }
        }
    }

    SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    // Colors with alpha blended over transparent black are premultiplied.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (const std::vector<SDL_Vertex>* layer : { &scenery, &glowVertices, &coreVertices }) {
        if (!layer->empty()) {
            SDL_RenderGeometry(renderer, nullptr, layer->data(), static_cast<int>(layer->size()), nullptr, 0);
        }
    }

    SDL_SetRenderTarget(renderer, previousTarget);

    textureScale = scale;
    textureValid = true;
}

void MuiMapView::build(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<MuiMapMarker>& markers)
{
    if (isEmpty()) {
        return;
    }

    float mapWidth = static_cast<float>(right - left + 1);
    float mapHeight = static_cast<float>(bottom - top + 1);

    // Whole explored part fits; zoom in up to a few dp per tile.
    float fitZoom = std::min(rect.w / mapWidth, rect.h / mapHeight);
    view.minZoom = fitZoom * 0.8f;
    view.maxZoom = std::max(ui.dp(28.0f), fitZoom * 1.5f);

    if (needsFit) {
        needsFit = false;
        view.zoom = fitZoom;
        view.centerX = left + mapWidth / 2.0f;
        view.centerY = top + mapHeight / 2.0f;
        if (hasCenter) {
            view.centerX = centerX;
            view.centerY = centerY;
        }
    }
    hasCenter = false;

    bool touched = ui.panZoom(id, rect, &view);
    view.zoom = std::clamp(view.zoom, view.minZoom, view.maxZoom);
    view.centerX = std::clamp(view.centerX, static_cast<float>(left), static_cast<float>(right + 1));
    view.centerY = std::clamp(view.centerY, static_cast<float>(top), static_cast<float>(bottom + 1));

    // Texture pixels per tile for this zoom (drawn again after a pinch, not
    // during it).
    float limit = std::min(kMaxTextureSize / mapWidth, kMaxTextureSize / mapHeight);
    float wanted = kScales[0];
    for (float scale : kScales) {
        if (scale <= view.zoom * 1.25f) {
            wanted = scale;
        }
    }
    wanted = std::min(wanted, limit);

    if (!textureValid || textureGeneration != muiDrawGeneration() || (!touched && std::fabs(wanted - textureScale) > 0.01f)) {
        render(ui, wanted);
    }

    muiPushClip(rect);

    if (texture != nullptr && textureValid) {
        MuiRect dest = {
            rect.centerX() + (left - view.centerX) * view.zoom,
            rect.centerY() + (top - view.centerY) * view.zoom,
            textureWidth / textureScale * view.zoom,
            textureHeight / textureScale * view.zoom,
        };
        muiDrawTexture(texture, dest);
    }

    for (const MuiMapMarker& marker : markers) {
        float x = rect.centerX() + (marker.x + 0.5f - view.centerX) * view.zoom;
        float y = rect.centerY() + (marker.y + 0.5f - view.centerY) * view.zoom;
        float radius = ui.dp(marker.radius);
        muiFillCircle(x, y, radius * 2.0f, marker.color.withAlpha(50));
        muiFillCircle(x, y, radius, marker.color);
    }

    muiPopClip();
}

} // namespace fallout
