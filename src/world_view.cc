#include "world_view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "art.h"
#include "map.h"
#include "map_edge.h"
#include "settings.h"
#include "svga.h"
#include "object.h"
#include "perf_monitor.h"
#include "tile.h"
#include "window_manager.h"
#include "world_texture.h"

namespace fallout {

// Extra space around the largest visible area. Must be at least one scroll
// step (see `mapScroll`) so sub-tile pan offset never reveals buffer edges.
static constexpr int kWorldMarginX = 64;
static constexpr int kWorldMarginY = 48;

static constexpr int kScrollStepX = 32;
static constexpr int kScrollStepY = 24;

enum WorldViewFilter {
    WORLD_VIEW_FILTER_NEAREST = 0,
    WORLD_VIEW_FILTER_LINEAR = 1,
    WORLD_VIEW_FILTER_SHARP = 2,
};

static void worldViewPanByWorld(float dx, float dy);
static void worldViewNeededRect(Rect* rect);
static bool worldViewScrollStep(int dx, int dy);
static void worldViewDraw(SDL_Renderer* renderer);
static void worldViewUpdatePalette();
static void worldViewRenderOverlay(SDL_Renderer* renderer, float centerX, float centerY);

static bool gWorldViewEnabled = false;
static int gWorldViewWindow = -1;

static unsigned char* gWorldBuffer = nullptr;
static int gWorldWidth = 0;
static int gWorldHeight = 0;

// On-screen map area, always at the top left corner of the screen.
static int gViewWidth = 0;
static int gViewHeight = 0;

static float gZoom = 1.0f;
static float gZoomMin = 1.0f;
static float gZoomMax = 1.0f;

// Offset of the view center from the world buffer center, in world pixels.
static float gPanX = 0.0f;
static float gPanY = 0.0f;
static bool gIsPanning = false;

// Palette for the overlay object (see `worldViewRenderOverlay`).
static Uint32 gPalette[256];

static int gFilter = WORLD_VIEW_FILTER_SHARP;

// Visible part of the world texture tiles composed at an integer scale (the
// final scaling samples one texture, so there are no seams between tiles);
// reused while the tiles, the source and the scale stay the same.
static SDL_Texture* gComposeTexture = nullptr;
static int gComposeTextureWidth = 0;
static int gComposeTextureHeight = 0;
static bool gComposeValid = false;
static unsigned int gComposeVersion = 0;
static SDL_Rect gComposeSource;
static int gComposeFactor = 0;

// Part of the buffer drawn and up to date (empty - nothing), see
// `worldViewClipRender`.
static Rect gDrawnRect = { 0, 0, -1, -1 };

// `worldViewRequire` draws beyond the view.
static bool gDrawAnywhere = false;

// A pinch is in progress, the view may be past the map's edges.
static bool gGestureActive = false;
static bool gEdgeCheckPending = false;

static Object* gOverlayObject = nullptr;
static int gOverlayAnchorX = 0;
static int gOverlayAnchorY = 0;
static SDL_Texture* gOverlayTexture = nullptr;
static int gOverlayTextureWidth = 0;
static int gOverlayTextureHeight = 0;

bool worldViewInit(int viewWidth, int viewHeight)
{
    worldViewExit();

    gViewWidth = viewWidth;
    gViewHeight = viewHeight;
    gWorldViewEnabled = settings.world_view.enabled;
    if (!gWorldViewEnabled) {
        return true;
    }

    gZoomMin = static_cast<float>(settings.world_view.zoom_min);
    gZoomMax = static_cast<float>(settings.world_view.zoom_max);
    gZoom = std::clamp(static_cast<float>(settings.world_view.zoom), gZoomMin, gZoomMax);
    gFilter = settings.world_view.filter;

    gWorldWidth = static_cast<int>(ceilf(viewWidth / gZoomMin)) + kWorldMarginX * 2;
    gWorldHeight = static_cast<int>(ceilf(viewHeight / gZoomMin)) + kWorldMarginY * 2;

    gWorldBuffer = reinterpret_cast<unsigned char*>(malloc(static_cast<size_t>(gWorldWidth) * gWorldHeight));
    if (gWorldBuffer == nullptr) {
        gWorldViewEnabled = false;
        return false;
    }

    memset(gWorldBuffer, 0, static_cast<size_t>(gWorldWidth) * gWorldHeight);
    worldTextureInit(gWorldBuffer, gWorldWidth, gWorldHeight);

    gPanX = 0.0f;
    gPanY = 0.0f;
    gDrawnRect = { 0, 0, -1, -1 };
    worldViewInvalidateAll();

    return true;
}

void worldViewExit()
{
    worldViewResetRenderer();

    if (gWorldBuffer != nullptr) {
        free(gWorldBuffer);
        gWorldBuffer = nullptr;
    }

    gWorldViewEnabled = false;
    gWorldViewWindow = -1;
    gOverlayObject = nullptr;
    worldTextureFree();
    gWorldWidth = 0;
    gWorldHeight = 0;
}

bool worldViewIsEnabled()
{
    return gWorldViewEnabled;
}

void worldViewSetWindow(int win)
{
    gWorldViewWindow = win;
}

bool worldViewIsWorldWindow(int win)
{
    return gWorldViewEnabled && win != -1 && win == gWorldViewWindow;
}

bool worldViewIsWorldAt(int x, int y)
{
    return gWorldViewEnabled && gWorldViewWindow != -1 && windowGetVisibleAtPoint(x, y) == gWorldViewWindow;
}

unsigned char* worldViewGetBuffer()
{
    return gWorldBuffer;
}

int worldViewGetWidth()
{
    return gWorldWidth;
}

int worldViewGetHeight()
{
    return gWorldHeight;
}

void worldViewGetViewCenter(int* x, int* y)
{
    if (!gWorldViewEnabled) {
        *x = gViewWidth / 2;
        *y = gViewHeight / 2;
        return;
    }

    *x = gWorldWidth / 2 + static_cast<int>(floorf(gPanX));
    *y = gWorldHeight / 2 + static_cast<int>(floorf(gPanY));
}

unsigned char* worldViewGetCenteredArea(int width, int height, int* pitch)
{
    if (!gWorldViewEnabled) {
        int windowWidth = windowGetWidth(gWorldViewWindow);
        int windowHeight = windowGetHeight(gWorldViewWindow);
        *pitch = windowWidth;
        return windowGetBuffer(gWorldViewWindow) + windowWidth * ((windowHeight - height) / 2) + (windowWidth - width) / 2;
    }

    int centerX;
    int centerY;
    worldViewGetViewCenter(&centerX, &centerY);

    int left = std::clamp(centerX - width / 2, 0, std::max(gWorldWidth - width, 0));
    int top = std::clamp(centerY - height / 2, 0, std::max(gWorldHeight - height, 0));

    Rect area = { left, top, left + width - 1, top + height - 1 };
    worldViewRequire(&area);

    *pitch = gWorldWidth;
    return gWorldBuffer + static_cast<size_t>(top) * gWorldWidth + left;
}

void worldViewInvalidate(const Rect* rect)
{
    if (!gWorldViewEnabled) {
        return;
    }

    worldTextureInvalidate(rect);
}

void worldViewInvalidateAll()
{
    if (!gWorldViewEnabled) {
        return;
    }

    worldTextureInvalidateAll();
}

void worldViewInvalidatePalette(int first, int count)
{
    (void)first;
    (void)count;
    worldTexturePaletteChanged();
}

void worldViewScrolled(int dx, int dy)
{
    if (!gWorldViewEnabled) {
        return;
    }

    worldTextureScrolled(dx, dy);

    // What was drawn moved with the contents.
    if (gDrawnRect.right >= gDrawnRect.left) {
        Rect buffer = { 0, 0, gWorldWidth - 1, gWorldHeight - 1 };
        Rect moved = { gDrawnRect.left - dx, gDrawnRect.top - dy, gDrawnRect.right - dx, gDrawnRect.bottom - dy };
        if (rectIntersection(&moved, &buffer, &gDrawnRect) != 0) {
            gDrawnRect = { 0, 0, -1, -1 };
        }
    }
}

// The view around the buffer's center with a scroll step on each side: the
// pan offset stays within a step (then the buffer scrolls), so the view is
// always inside. Doesn't move with the fingers, only with the zoom.
static void worldViewNeededRect(Rect* rect)
{
    int width;
    int height;
    worldViewGetVisibleSize(&width, &height);
    int halfWidth = width / 2 + 1 + kScrollStepX;
    int halfHeight = height / 2 + 1 + kScrollStepY;
    rect->left = std::max(gWorldWidth / 2 - halfWidth, 0);
    rect->top = std::max(gWorldHeight / 2 - halfHeight, 0);
    rect->right = std::min(gWorldWidth / 2 + halfWidth, gWorldWidth - 1);
    rect->bottom = std::min(gWorldHeight / 2 + halfHeight, gWorldHeight - 1);
}

static bool rectContains(const Rect& outer, const Rect& inner)
{
    return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right && outer.bottom >= inner.bottom;
}

bool worldViewClipRender(Rect* rect)
{
    if (!gWorldViewEnabled || gDrawAnywhere) {
        return true;
    }

    Rect needed;
    worldViewNeededRect(&needed);
    return rectIntersection(rect, &needed, rect) == 0;
}

void worldViewRendered(const Rect* rect)
{
    if (!gWorldViewEnabled || gDrawAnywhere) {
        return;
    }

    Rect needed;
    worldViewNeededRect(&needed);
    if (rectContains(*rect, needed)) {
        gDrawnRect = needed;
        return;
    }

    // A band along a side of the drawn part, as long as the side: together
    // they are a rect (see `worldViewEnsureRendered`).
    Rect& drawn = gDrawnRect;
    if (drawn.right < drawn.left) {
        return;
    }

    if (rect->top == drawn.top && rect->bottom == drawn.bottom && rect->left <= drawn.right + 1 && rect->right >= drawn.left - 1) {
        drawn.left = std::min(drawn.left, rect->left);
        drawn.right = std::max(drawn.right, rect->right);
    } else if (rect->left == drawn.left && rect->right == drawn.right && rect->top <= drawn.bottom + 1 && rect->bottom >= drawn.top - 1) {
        drawn.top = std::min(drawn.top, rect->top);
        drawn.bottom = std::max(drawn.bottom, rect->bottom);
    }
}

void worldViewEnsureRendered()
{
    // Panning scrolls step by step and ensures at its end.
    if (!gWorldViewEnabled || gIsPanning) {
        return;
    }

    Rect needed;
    worldViewNeededRect(&needed);

    Rect drawn;
    if (gDrawnRect.right < gDrawnRect.left || rectIntersection(&gDrawnRect, &needed, &drawn) != 0) {
        gDrawnRect = { 0, 0, -1, -1 };
        tileWindowRefreshRect(&needed, gElevation);
        return;
    }

    gDrawnRect = drawn;
    if (rectContains(drawn, needed)) {
        return;
    }

    // The sides first, then the whole width above and below: each band
    // continues the drawn rect.
    Rect bands[] = {
        { needed.left, drawn.top, drawn.left - 1, drawn.bottom },
        { drawn.right + 1, drawn.top, needed.right, drawn.bottom },
        { needed.left, needed.top, needed.right, drawn.top - 1 },
        { needed.left, drawn.bottom + 1, needed.right, needed.bottom },
    };
    for (Rect& band : bands) {
        if (band.left <= band.right && band.top <= band.bottom) {
            tileWindowRefreshRect(&band, gElevation);
        }
    }
}

bool worldViewIsPanning()
{
    return gWorldViewEnabled && gIsPanning;
}

void worldViewForgetRendered()
{
    gDrawnRect = { 0, 0, -1, -1 };
}

void worldViewRequire(const Rect* rect)
{
    if (!gWorldViewEnabled || rectContains(gDrawnRect, *rect)) {
        return;
    }

    Rect area = *rect;
    gDrawAnywhere = true;
    tileWindowRefreshRect(&area, gElevation);
    gDrawAnywhere = false;
}

void worldViewBeginGesture()
{
    gGestureActive = true;
}

void worldViewEndGesture()
{
    gGestureActive = false;

    if (gEdgeCheckPending) {
        gEdgeCheckPending = false;
        mapEdgeHandleViewSizeChanged(true);
        worldViewEnsureRendered();
    }
}

void worldViewScreenToWorld(int screenX, int screenY, int* worldX, int* worldY)
{
    if (!gWorldViewEnabled) {
        *worldX = screenX;
        *worldY = screenY;
        return;
    }

    float centerX = gWorldWidth / 2 + gPanX;
    float centerY = gWorldHeight / 2 + gPanY;
    *worldX = static_cast<int>(floorf((screenX + 0.5f - gViewWidth / 2) / gZoom + centerX));
    *worldY = static_cast<int>(floorf((screenY + 0.5f - gViewHeight / 2) / gZoom + centerY));
}

void worldViewWorldToScreen(int worldX, int worldY, int* screenX, int* screenY)
{
    if (!gWorldViewEnabled) {
        *screenX = worldX;
        *screenY = worldY;
        return;
    }

    float centerX = gWorldWidth / 2 + gPanX;
    float centerY = gWorldHeight / 2 + gPanY;
    *screenX = static_cast<int>(floorf((worldX + 0.5f - centerX) * gZoom + gViewWidth / 2));
    *screenY = static_cast<int>(floorf((worldY + 0.5f - centerY) * gZoom + gViewHeight / 2));
}

void worldViewGetVisibleRect(Rect* rect)
{
    if (!gWorldViewEnabled) {
        rect->left = 0;
        rect->top = 0;
        rect->right = gViewWidth - 1;
        rect->bottom = gViewHeight - 1;
        return;
    }

    worldViewScreenToWorld(0, 0, &(rect->left), &(rect->top));
    worldViewScreenToWorld(gViewWidth - 1, gViewHeight - 1, &(rect->right), &(rect->bottom));
}

void worldViewGetVisibleSize(int* width, int* height)
{
    if (!gWorldViewEnabled) {
        *width = gViewWidth;
        *height = gViewHeight;
        return;
    }

    *width = static_cast<int>(ceilf(gViewWidth / gZoom));
    *height = static_cast<int>(ceilf(gViewHeight / gZoom));
}

float worldViewGetZoom()
{
    return gWorldViewEnabled ? gZoom : 1.0f;
}

void worldViewSetZoom(float zoom, float anchorX, float anchorY)
{
    if (!gWorldViewEnabled) {
        return;
    }

    zoom = std::clamp(zoom, gZoomMin, gZoomMax);
    if (zoom == gZoom) {
        return;
    }

    // Keep the world point under the anchor in place.
    float anchorOffsetX = anchorX - gViewWidth / 2;
    float anchorOffsetY = anchorY - gViewHeight / 2;
    float dx = anchorOffsetX / gZoom - anchorOffsetX / zoom;
    float dy = anchorOffsetY / gZoom - anchorOffsetY / zoom;

    gZoom = zoom;

    // Scroll limits depend on visible size. While fingers zoom, the view is
    // moved back inside the map's edges when they're lifted.
    if (gGestureActive) {
        gEdgeCheckPending = true;
    }
    mapEdgeHandleViewSizeChanged(!gGestureActive);

    worldViewPanByWorld(dx, dy);
}

void worldViewZoomBy(float factor, float anchorX, float anchorY)
{
    worldViewSetZoom(gZoom * factor, anchorX, anchorY);
}

void worldViewPanBy(float dx, float dy)
{
    if (!gWorldViewEnabled) {
        return;
    }

    worldViewPanByWorld(-dx / gZoom, -dy / gZoom);
}

// Moves view center by the given distance in world pixels. Whole scroll steps
// are delegated to the tile engine, the remainder is kept as sub-tile offset.
static void worldViewPanByWorld(float dx, float dy)
{
    gPanX += dx;
    gPanY += dy;

    gIsPanning = true;

    while (gPanX >= kScrollStepX) {
        if (!worldViewScrollStep(1, 0)) {
            gPanX = kScrollStepX - 1;
            break;
        }
        gPanX -= kScrollStepX;
    }

    while (gPanX <= -kScrollStepX) {
        if (!worldViewScrollStep(-1, 0)) {
            gPanX = -(kScrollStepX - 1);
            break;
        }
        gPanX += kScrollStepX;
    }

    while (gPanY >= kScrollStepY) {
        if (!worldViewScrollStep(0, 1)) {
            gPanY = kScrollStepY - 1;
            break;
        }
        gPanY -= kScrollStepY;
    }

    while (gPanY <= -kScrollStepY) {
        if (!worldViewScrollStep(0, -1)) {
            gPanY = -(kScrollStepY - 1);
            break;
        }
        gPanY += kScrollStepY;
    }

    gIsPanning = false;

    worldViewEnsureRendered();
}

// Scrolls map by one step. On map edges tile engine may move the view (and
// redraw it) while still reporting an error, so success is determined by
// the center tile change.
static bool worldViewScrollStep(int dx, int dy)
{
    int oldCenterTile = gCenterTile;
    return mapScrollImmediate(dx, dy) == 0 || gCenterTile != oldCenterTile;
}

void worldViewHandleCenterChanged()
{
    if (!gIsPanning) {
        gPanX = 0.0f;
        gPanY = 0.0f;
    }
}

static void worldViewUpdatePalette()
{
    if (gSdlSurface == nullptr || gSdlSurface->format->palette == nullptr) {
        return;
    }

    SDL_Color* colors = gSdlSurface->format->palette->colors;
    for (int index = 0; index < 256; index++) {
        gPalette[index] = 0xFF000000 | (colors[index].r << 16) | (colors[index].g << 8) | colors[index].b;
    }
}

void worldViewRender(SDL_Renderer* renderer)
{
    if (!gWorldViewEnabled || gWorldBuffer == nullptr) {
        return;
    }

    Uint64 uploadStart;
    perfMonitorMark(&uploadStart);
    bool ready = worldTextureUpload(renderer);
    perfMonitorSpan(PerfSpan::WorldUpload, uploadStart);
    if (!ready) {
        return;
    }

    Uint64 drawStart;
    perfMonitorMark(&drawStart);
    worldViewDraw(renderer);
    perfMonitorSpan(PerfSpan::WorldDraw, drawStart);
}

// Draws the visible part of the world texture (see `worldViewRender`).
static void worldViewDraw(SDL_Renderer* renderer)
{
    float centerX = gWorldWidth / 2 + gPanX;
    float centerY = gWorldHeight / 2 + gPanY;
    float halfViewWidth = gViewWidth / 2;
    float halfViewHeight = gViewHeight / 2;

    // Integer source rect covering the visible area (with a 1px guard band
    // for filtering), and matching fractional destination rect.
    int left = std::max(static_cast<int>(floorf(centerX - halfViewWidth / gZoom)) - 1, 0);
    int top = std::max(static_cast<int>(floorf(centerY - halfViewHeight / gZoom)) - 1, 0);
    int right = std::min(static_cast<int>(ceilf(centerX + (gViewWidth - halfViewWidth) / gZoom)) + 1, gWorldWidth);
    int bottom = std::min(static_cast<int>(ceilf(centerY + (gViewHeight - halfViewHeight) / gZoom)) + 1, gWorldHeight);
    if (left >= right || top >= bottom) {
        return;
    }

    SDL_Rect srcRect;
    srcRect.x = left;
    srcRect.y = top;
    srcRect.w = right - left;
    srcRect.h = bottom - top;

    SDL_FRect destRect;
    destRect.x = (left - centerX) * gZoom + halfViewWidth;
    destRect.y = (top - centerY) * gZoom + halfViewHeight;
    destRect.w = srcRect.w * gZoom;
    destRect.h = srcRect.h * gZoom;

    // Total scale from world pixels to physical pixels.
    float renderScaleX;
    float renderScaleY;
    SDL_RenderGetScale(renderer, &renderScaleX, &renderScaleY);
    float scale = gZoom * std::min(renderScaleX, renderScaleY);

    // Nearest: composed 1:1, scaled with nearest. Linear: composed 1:1,
    // scaled with bilinear. Sharp bilinear: composed with nearest by the
    // largest integer factor, the remaining fractional part is smoothed.
    int factor = 1;
    SDL_ScaleMode scaleMode = SDL_ScaleModeLinear;
    switch (gFilter) {
    case WORLD_VIEW_FILTER_NEAREST:
        scaleMode = SDL_ScaleModeNearest;
        break;
    case WORLD_VIEW_FILTER_SHARP:
        factor = std::max(static_cast<int>(floorf(scale + 0.001f)), 1);
        if (fabsf(scale - factor) < 0.001f) {
            scaleMode = SDL_ScaleModeNearest;
        }
        break;
    default:
        break;
    }

    int width = srcRect.w * factor;
    int height = srcRect.h * factor;
    if (gComposeTexture == nullptr || gComposeTextureWidth < width || gComposeTextureHeight < height) {
        if (gComposeTexture != nullptr) {
            SDL_DestroyTexture(gComposeTexture);
        }

        gComposeTextureWidth = std::max(width, gComposeTextureWidth);
        gComposeTextureHeight = std::max(height, gComposeTextureHeight);
        gComposeTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, gComposeTextureWidth, gComposeTextureHeight);
        if (gComposeTexture == nullptr) {
            gComposeTextureWidth = 0;
            gComposeTextureHeight = 0;
            return;
        }
        SDL_SetTextureBlendMode(gComposeTexture, SDL_BLENDMODE_NONE);
        gComposeValid = false;
    }

    bool sameSource = srcRect.x == gComposeSource.x && srcRect.y == gComposeSource.y && srcRect.w == gComposeSource.w && srcRect.h == gComposeSource.h;
    if (!gComposeValid || gComposeVersion != worldTextureVersion() || gComposeFactor != factor || !sameSource) {
        if (SDL_SetRenderTarget(renderer, gComposeTexture) != 0) {
            return;
        }
        worldTextureDraw(renderer, srcRect, factor);
        SDL_SetRenderTarget(renderer, nullptr);

        gComposeValid = true;
        gComposeVersion = worldTextureVersion();
        gComposeFactor = factor;
        gComposeSource = srcRect;
    }

    SDL_SetTextureScaleMode(gComposeTexture, scaleMode);

    SDL_Rect textureRect = { 0, 0, width, height };

    SDL_Rect clipRect;
    clipRect.x = 0;
    clipRect.y = 0;
    clipRect.w = gViewWidth;
    clipRect.h = gViewHeight;
    SDL_RenderSetClipRect(renderer, &clipRect);
    SDL_RenderCopyF(renderer, gComposeTexture, &textureRect, &destRect);
    worldViewRenderOverlay(renderer, centerX, centerY);
    SDL_RenderSetClipRect(renderer, nullptr);
}

void worldViewSetFilter(int filter)
{
    gFilter = std::clamp(filter, 0, 2);
    gComposeValid = false;
}

bool worldViewCaptureView(std::vector<unsigned char>* rgba, int* width, int* height, int maxWidth)
{
    if (!gWorldViewEnabled || gWorldBuffer == nullptr || gSdlSurface == nullptr || gSdlSurface->format->palette == nullptr) {
        return false;
    }

    Rect visible;
    worldViewGetVisibleRect(&visible);
    visible.left = std::max(visible.left, 0);
    visible.top = std::max(visible.top, 0);
    visible.right = std::min(visible.right, gWorldWidth - 1);
    visible.bottom = std::min(visible.bottom, gWorldHeight - 1);
    int sourceWidth = visible.right - visible.left + 1;
    int sourceHeight = visible.bottom - visible.top + 1;
    if (sourceWidth <= 0 || sourceHeight <= 0 || maxWidth <= 0) {
        return false;
    }

    *width = std::min(sourceWidth, maxWidth);
    *height = std::max(sourceHeight * *width / sourceWidth, 1);

    const SDL_Color* colors = gSdlSurface->format->palette->colors;
    rgba->resize(static_cast<size_t>(*width) * *height * 4);
    for (int y = 0; y < *height; y++) {
        const unsigned char* row = gWorldBuffer + static_cast<size_t>(visible.top + y * sourceHeight / *height) * gWorldWidth + visible.left;
        unsigned char* dest = rgba->data() + static_cast<size_t>(y) * *width * 4;
        for (int x = 0; x < *width; x++) {
            const SDL_Color& color = colors[row[x * sourceWidth / *width]];
            dest[x * 4] = color.r;
            dest[x * 4 + 1] = color.g;
            dest[x * 4 + 2] = color.b;
            dest[x * 4 + 3] = 255;
        }
    }

    return true;
}

void worldViewSetOverlayObject(Object* object, int anchorX, int anchorY)
{
    gOverlayObject = gWorldViewEnabled ? object : nullptr;
    gOverlayAnchorX = anchorX;
    gOverlayAnchorY = anchorY;
}

bool worldViewIsOverlayObject(Object* object)
{
    return object != nullptr && object == gOverlayObject;
}

// Draws overlay object at its anchor point without zooming, so it keeps the
// size of the UI.
static void worldViewRenderOverlay(SDL_Renderer* renderer, float centerX, float centerY)
{
    Object* object = gOverlayObject;
    if (object == nullptr || (object->flags & OBJECT_HIDDEN) != 0) {
        return;
    }

    worldViewUpdatePalette();

    CacheEntry* handle;
    Art* art = artLock(FrmId(object), &handle);
    if (art == nullptr) {
        return;
    }

    int width = artGetWidth(art, object->frame, object->rotation);
    int height = artGetHeight(art, object->frame, object->rotation);
    unsigned char* data = artGetFrameData(art, object->frame, object->rotation);
    if (data == nullptr || width <= 0 || height <= 0) {
        artUnlock(handle);
        return;
    }

    if (gOverlayTexture == nullptr || gOverlayTextureWidth < width || gOverlayTextureHeight < height) {
        if (gOverlayTexture != nullptr) {
            SDL_DestroyTexture(gOverlayTexture);
        }

        gOverlayTextureWidth = std::max(width, gOverlayTextureWidth);
        gOverlayTextureHeight = std::max(height, gOverlayTextureHeight);
        gOverlayTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, gOverlayTextureWidth, gOverlayTextureHeight);
        if (gOverlayTexture == nullptr) {
            artUnlock(handle);
            return;
        }

        SDL_SetTextureBlendMode(gOverlayTexture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(gOverlayTexture, SDL_ScaleModeNearest);
    }

    SDL_Rect textureRect;
    textureRect.x = 0;
    textureRect.y = 0;
    textureRect.w = width;
    textureRect.h = height;

    void* pixels;
    int pitch;
    if (SDL_LockTexture(gOverlayTexture, &textureRect, &pixels, &pitch) != 0) {
        artUnlock(handle);
        return;
    }

    // Palette index 0 is transparent.
    for (int y = 0; y < height; y++) {
        const unsigned char* src = data + static_cast<size_t>(y) * width;
        Uint32* dest = reinterpret_cast<Uint32*>(reinterpret_cast<unsigned char*>(pixels) + static_cast<size_t>(y) * pitch);
        for (int x = 0; x < width; x++) {
            dest[x] = src[x] != 0 ? gPalette[src[x]] : 0;
        }
    }

    SDL_UnlockTexture(gOverlayTexture);
    artUnlock(handle);

    // Object rect is laid out in world coordinates relative to the anchor.
    // Keep this layout in screen pixels.
    Rect rect;
    objectGetRect(object, &rect);

    SDL_FRect destRect;
    destRect.x = (gOverlayAnchorX + 0.5f - centerX) * gZoom + gViewWidth / 2 + (rect.left - gOverlayAnchorX);
    destRect.y = (gOverlayAnchorY + 0.5f - centerY) * gZoom + gViewHeight / 2 + (rect.top - gOverlayAnchorY);
    destRect.w = static_cast<float>(width);
    destRect.h = static_cast<float>(height);

    SDL_RenderCopyF(renderer, gOverlayTexture, &textureRect, &destRect);
}

void worldViewResetRenderer()
{
    if (gOverlayTexture != nullptr) {
        SDL_DestroyTexture(gOverlayTexture);
        gOverlayTexture = nullptr;
        gOverlayTextureWidth = 0;
        gOverlayTextureHeight = 0;
    }

    if (gComposeTexture != nullptr) {
        SDL_DestroyTexture(gComposeTexture);
        gComposeTexture = nullptr;
        gComposeTextureWidth = 0;
        gComposeTextureHeight = 0;
    }
    gComposeValid = false;

    worldTextureResetRenderer();
}

} // namespace fallout
