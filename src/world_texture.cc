#include "world_texture.h"

#include <string.h>

#include <algorithm>
#include <vector>

#include "perf_monitor.h"
#include "svga.h"

namespace fallout {

namespace {

    constexpr int kTileSize = 256;

    // Palette entries the game cycles (see cycle.cc).
    constexpr int kCyclingPaletteFirst = 229;
    constexpr int kCyclingPaletteLast = 254;

    struct Tile {
        SDL_Texture* texture = nullptr;

        // Area of the ring (texture space) the tile holds.
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;

        // Changed part waiting for upload (tile coordinates).
        bool dirty = false;
        Rect dirtyRect;

        // Cycling palette entries the tile's pixels use (bit per entry) and
        // where; found when uploading, grows until the whole tile is
        // uploaded again.
        Uint32 cyclingEntries = 0;
        Rect cyclingRect;
    };

    unsigned char* gBuffer = nullptr;
    int gWidth = 0;
    int gHeight = 0;

    // Ring position of the buffer's top left pixel: buffer pixel (x, y) is
    // at ((x + gOriginX) mod width, (y + gOriginY) mod height).
    int gOriginX = 0;
    int gOriginY = 0;

    std::vector<Tile> gTiles;
    int gTileColumns = 0;
    int gTileRows = 0;
    bool gTexturesMade = false;

    Uint32 gPalette[256];
    bool gPaletteKnown = false;
    bool gPaletteChanged = false;

    unsigned int gVersion = 0;

    int wrap(int value, int size)
    {
        value %= size;
        return value < 0 ? value + size : value;
    }

    void markTile(Tile& tile, const Rect& rect)
    {
        if (tile.dirty) {
            tile.dirtyRect.left = std::min(tile.dirtyRect.left, rect.left);
            tile.dirtyRect.top = std::min(tile.dirtyRect.top, rect.top);
            tile.dirtyRect.right = std::max(tile.dirtyRect.right, rect.right);
            tile.dirtyRect.bottom = std::max(tile.dirtyRect.bottom, rect.bottom);
        } else {
            tile.dirtyRect = rect;
            tile.dirty = true;
        }
    }

    // Area of the ring (inclusive), not crossing its edges.
    void invalidateRingRect(int left, int top, int right, int bottom)
    {
        for (int row = top / kTileSize; row <= bottom / kTileSize; row++) {
            for (int column = left / kTileSize; column <= right / kTileSize; column++) {
                Tile& tile = gTiles[row * gTileColumns + column];
                Rect rect;
                rect.left = std::max(left, tile.x) - tile.x;
                rect.top = std::max(top, tile.y) - tile.y;
                rect.right = std::min(right, tile.x + tile.width - 1) - tile.x;
                rect.bottom = std::min(bottom, tile.y + tile.height - 1) - tile.y;
                markTile(tile, rect);
            }
        }
    }

    // Parts of [start, start + length) of the buffer along one axis as spans
    // of the ring: ring start, length, offset from [start].
    struct Span {
        int ring;
        int length;
        int offset;
    };

    int ringSpans(int start, int length, int origin, int size, Span* spans)
    {
        int ring = wrap(start + origin, size);
        if (ring + length <= size) {
            spans[0] = { ring, length, 0 };
            return 1;
        }

        int first = size - ring;
        spans[0] = { ring, first, 0 };
        spans[1] = { 0, length - first, first };
        return 2;
    }

    void updatePalette(Uint32* palette)
    {
        SDL_Color* colors = gSdlSurface->format->palette->colors;
        for (int index = 0; index < 256; index++) {
            palette[index] = 0xFF000000 | (colors[index].r << 16) | (colors[index].g << 8) | colors[index].b;
        }
    }

    // Finds palette entries which really changed (the game sets all cycling
    // entries when any of them moves) and marks what uses them.
    void applyPaletteChange()
    {
        gPaletteChanged = false;
        if (gSdlSurface == nullptr || gSdlSurface->format->palette == nullptr) {
            return;
        }

        Uint32 palette[256];
        updatePalette(palette);

        if (!gPaletteKnown) {
            memcpy(gPalette, palette, sizeof(gPalette));
            gPaletteKnown = true;
            worldTextureInvalidateAll();
            return;
        }

        bool otherChanged = false;
        Uint32 cyclingChanged = 0;
        for (int index = 0; index < 256; index++) {
            if (palette[index] == gPalette[index]) {
                continue;
            }
            if (index >= kCyclingPaletteFirst && index <= kCyclingPaletteLast) {
                cyclingChanged |= 1u << (index - kCyclingPaletteFirst);
            } else {
                otherChanged = true;
            }
        }

        memcpy(gPalette, palette, sizeof(gPalette));

        if (otherChanged) {
            worldTextureInvalidateAll();
            return;
        }

        if (cyclingChanged != 0) {
            for (Tile& tile : gTiles) {
                if ((tile.cyclingEntries & cyclingChanged) != 0) {
                    markTile(tile, tile.cyclingRect);
                }
            }
        }
    }

    bool makeTextures(SDL_Renderer* renderer)
    {
        for (Tile& tile : gTiles) {
            tile.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kTileSize, kTileSize);
            if (tile.texture == nullptr) {
                worldTextureResetRenderer();
                return false;
            }

            SDL_SetTextureBlendMode(tile.texture, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(tile.texture, SDL_ScaleModeNearest);
        }

        gTexturesMade = true;

        // New textures have undefined content.
        gPaletteKnown = false;
        gPaletteChanged = true;
        return true;
    }

    void uploadTile(Tile& tile)
    {
        tile.dirty = false;

        SDL_Rect rect;
        rect.x = tile.dirtyRect.left;
        rect.y = tile.dirtyRect.top;
        rect.w = tile.dirtyRect.right - tile.dirtyRect.left + 1;
        rect.h = tile.dirtyRect.bottom - tile.dirtyRect.top + 1;

        void* pixels;
        int pitch;
        if (SDL_LockTexture(tile.texture, &rect, &pixels, &pitch) != 0) {
            return;
        }

        Uint32 entries = 0;
        Rect cycling = { rect.w, rect.h, -1, -1 };

        for (int y = 0; y < rect.h; y++) {
            int bufferY = wrap(tile.y + rect.y + y - gOriginY, gHeight);
            const unsigned char* src = gBuffer + static_cast<size_t>(bufferY) * gWidth;
            int bufferX = wrap(tile.x + rect.x - gOriginX, gWidth);
            Uint32* dest = reinterpret_cast<Uint32*>(reinterpret_cast<unsigned char*>(pixels) + static_cast<size_t>(y) * pitch);
            for (int x = 0; x < rect.w; x++) {
                unsigned char index = src[bufferX];
                dest[x] = gPalette[index];
                if (index >= kCyclingPaletteFirst && index <= kCyclingPaletteLast) {
                    entries |= 1u << (index - kCyclingPaletteFirst);
                    cycling.left = std::min(cycling.left, x);
                    cycling.top = std::min(cycling.top, y);
                    cycling.right = std::max(cycling.right, x);
                    cycling.bottom = std::max(cycling.bottom, y);
                }

                if (++bufferX == gWidth) {
                    bufferX = 0;
                }
            }
        }

        SDL_UnlockTexture(tile.texture);

        perfMonitorCount(PerfCount::WorldUploads, 1);
        perfMonitorCount(PerfCount::WorldPixels, static_cast<long long>(rect.w) * rect.h);

        // The whole tile was uploaded: what it uses is known exactly.
        if (rect.w == tile.width && rect.h == tile.height) {
            tile.cyclingEntries = 0;
        }

        if (entries != 0) {
            Rect found = { rect.x + cycling.left, rect.y + cycling.top, rect.x + cycling.right, rect.y + cycling.bottom };
            if (tile.cyclingEntries == 0) {
                tile.cyclingRect = found;
            } else {
                tile.cyclingRect.left = std::min(tile.cyclingRect.left, found.left);
                tile.cyclingRect.top = std::min(tile.cyclingRect.top, found.top);
                tile.cyclingRect.right = std::max(tile.cyclingRect.right, found.right);
                tile.cyclingRect.bottom = std::max(tile.cyclingRect.bottom, found.bottom);
            }
            tile.cyclingEntries |= entries;
        }
    }

} // namespace

bool worldTextureInit(unsigned char* buffer, int width, int height)
{
    worldTextureFree();

    gBuffer = buffer;
    gWidth = width;
    gHeight = height;
    gOriginX = 0;
    gOriginY = 0;

    gTileColumns = (width + kTileSize - 1) / kTileSize;
    gTileRows = (height + kTileSize - 1) / kTileSize;
    gTiles.resize(static_cast<size_t>(gTileColumns) * gTileRows);
    for (int row = 0; row < gTileRows; row++) {
        for (int column = 0; column < gTileColumns; column++) {
            Tile& tile = gTiles[row * gTileColumns + column];
            tile.x = column * kTileSize;
            tile.y = row * kTileSize;
            tile.width = std::min(kTileSize, width - tile.x);
            tile.height = std::min(kTileSize, height - tile.y);
        }
    }

    gPaletteKnown = false;
    gPaletteChanged = true;
    return true;
}

void worldTextureFree()
{
    worldTextureResetRenderer();
    gTiles.clear();
    gTileColumns = 0;
    gTileRows = 0;
    gBuffer = nullptr;
    gWidth = 0;
    gHeight = 0;
}

void worldTextureResetRenderer()
{
    for (Tile& tile : gTiles) {
        if (tile.texture != nullptr) {
            SDL_DestroyTexture(tile.texture);
            tile.texture = nullptr;
        }
    }
    gTexturesMade = false;
}

void worldTextureInvalidate(const Rect* rect)
{
    if (gTiles.empty()) {
        return;
    }

    int left = std::max(rect->left, 0);
    int top = std::max(rect->top, 0);
    int right = std::min(rect->right, gWidth - 1);
    int bottom = std::min(rect->bottom, gHeight - 1);
    if (left > right || top > bottom) {
        return;
    }

    Span columns[2];
    Span rows[2];
    int columnCount = ringSpans(left, right - left + 1, gOriginX, gWidth, columns);
    int rowCount = ringSpans(top, bottom - top + 1, gOriginY, gHeight, rows);
    for (int row = 0; row < rowCount; row++) {
        for (int column = 0; column < columnCount; column++) {
            invalidateRingRect(columns[column].ring,
                rows[row].ring,
                columns[column].ring + columns[column].length - 1,
                rows[row].ring + rows[row].length - 1);
        }
    }
}

void worldTextureInvalidateAll()
{
    for (Tile& tile : gTiles) {
        markTile(tile, { 0, 0, tile.width - 1, tile.height - 1 });
    }
}

void worldTexturePaletteChanged()
{
    gPaletteChanged = true;
}

void worldTextureScrolled(int dx, int dy)
{
    if (gTiles.empty()) {
        return;
    }

    gOriginX = wrap(gOriginX + dx, gWidth);
    gOriginY = wrap(gOriginY + dy, gHeight);
    gVersion++;
}

bool worldTextureUpload(SDL_Renderer* renderer)
{
    if (gTiles.empty() || gBuffer == nullptr) {
        return false;
    }

    if (!gTexturesMade && !makeTextures(renderer)) {
        return false;
    }

    if (gPaletteChanged) {
        applyPaletteChange();
    }

    bool uploaded = false;
    for (Tile& tile : gTiles) {
        if (tile.dirty) {
            uploadTile(tile);
            uploaded = true;
        }
    }

    if (uploaded) {
        gVersion++;
    }

    return true;
}

void worldTextureDraw(SDL_Renderer* renderer, const SDL_Rect& source, int factor)
{
    Span columns[2];
    Span rows[2];
    int columnCount = ringSpans(source.x, source.w, gOriginX, gWidth, columns);
    int rowCount = ringSpans(source.y, source.h, gOriginY, gHeight, rows);

    for (int row = 0; row < rowCount; row++) {
        const Span& rowSpan = rows[row];
        for (int column = 0; column < columnCount; column++) {
            const Span& columnSpan = columns[column];

            int ringLeft = columnSpan.ring;
            int ringTop = rowSpan.ring;
            int ringRight = ringLeft + columnSpan.length - 1;
            int ringBottom = ringTop + rowSpan.length - 1;

            for (int tileRow = ringTop / kTileSize; tileRow <= ringBottom / kTileSize; tileRow++) {
                for (int tileColumn = ringLeft / kTileSize; tileColumn <= ringRight / kTileSize; tileColumn++) {
                    const Tile& tile = gTiles[tileRow * gTileColumns + tileColumn];
                    int left = std::max(ringLeft, tile.x);
                    int top = std::max(ringTop, tile.y);
                    int right = std::min(ringRight, tile.x + tile.width - 1);
                    int bottom = std::min(ringBottom, tile.y + tile.height - 1);

                    SDL_Rect src = { left - tile.x, top - tile.y, right - left + 1, bottom - top + 1 };
                    SDL_Rect dest = {
                        (columnSpan.offset + left - ringLeft) * factor,
                        (rowSpan.offset + top - ringTop) * factor,
                        src.w * factor,
                        src.h * factor,
                    };
                    SDL_RenderCopy(renderer, tile.texture, &src, &dest);
                }
            }
        }
    }
}

unsigned int worldTextureVersion()
{
    return gVersion;
}

} // namespace fallout
