#ifndef FALLOUT_WORLD_VIEW_H_
#define FALLOUT_WORLD_VIEW_H_

#include <SDL.h>

#include <vector>

#include "geometry.h"
#include "obj_types.h"

namespace fallout {

// CE: World view separates the isometric map from the rest of the UI so the
// map can be zoomed and panned smoothly on the GPU.
//
// When enabled, tiles and objects are rendered into a dedicated world buffer
// which is larger than the on-screen map area (so the view can be zoomed out
// and panned by sub-tile offsets). The isometric window stays in the window
// manager as a placeholder: every screen pixel that belongs to it is marked
// with `kScreenLayerWorld`, which makes the UI texture transparent there and
// lets the separately scaled world texture show through.
//
// When disabled the world buffer is the isometric window buffer itself and the
// engine behaves exactly as before.

constexpr unsigned char kScreenLayerUi = 0;
constexpr unsigned char kScreenLayerWorld = 1;

// Initializes world view for the on-screen map area of the given size. Must be
// called before `tileInit`/`objectsInit`, which should render into
// `worldViewGetBuffer` when world view is enabled.
bool worldViewInit(int viewWidth, int viewHeight);
void worldViewExit();

bool worldViewIsEnabled();

// Sets the window which acts as a placeholder for the world on screen.
void worldViewSetWindow(int win);
bool worldViewIsWorldWindow(int win);

// Returns true if the map is visible at the given screen point (i.e. it's not
// covered by any other window).
bool worldViewIsWorldAt(int x, int y);

// World buffer (8-bit palette indices), only valid when world view is enabled.
unsigned char* worldViewGetBuffer();
int worldViewGetWidth();
int worldViewGetHeight();

// Returns world buffer coordinates of the point shown in the center of the
// on-screen map area.
void worldViewGetViewCenter(int* x, int* y);

// Returns pointer to the area of the given size in the center of the map view
// (in world buffer when enabled, in isometric window otherwise).
unsigned char* worldViewGetCenteredArea(int width, int height, int* pitch);

// Marks area of the world buffer as changed.
void worldViewInvalidate(const Rect* rect);
void worldViewInvalidateAll();
// Palette entries [first, first + count) changed: pixels using them are
// uploaded again (only where they are, for the entries the game cycles).
void worldViewInvalidatePalette(int first, int count);

// The map scrolled by (dx, dy) world pixels: the buffer contents moved by
// (-dx, -dy) (see `mapScrollImmediate`). Must be called before the uncovered
// strips are drawn.
void worldViewScrolled(int dx, int dy);

// Only the part of the buffer on screen, with a scroll step around it, is
// kept drawn; the rest is drawn when it comes into view (panning, zooming
// out). Redrawing the map (roofs, a new center) costs what's on screen, not
// the whole buffer made for the smallest zoom.
//
// Tile engine (`tileRefreshGame`): the part of [rect] to draw now, false -
// none.
bool worldViewClipRender(Rect* rect);
// Tile engine: [rect] was drawn.
void worldViewRendered(const Rect* rect);
// Draws what is in view but not drawn yet (after the view or the buffer
// moved).
void worldViewEnsureRendered();
// The buffer was filled with something else (cleared): nothing is drawn.
void worldViewForgetRendered();
// The view is being moved by several scroll steps: the strips they uncover
// are drawn together after the last one (`worldViewEnsureRendered`).
bool worldViewIsPanning();
// Draws [rect] of the buffer if it isn't (reading the buffer beyond the
// view: save thumbnails).
void worldViewRequire(const Rect* rect);

// Fingers pinch the map: the view may go past the map's edges meanwhile and
// is moved back when the gesture ends (moving it back every frame redraws
// the whole map).
void worldViewBeginGesture();
void worldViewEndGesture();

// Returns part of the world buffer visible on screen. When world view is
// disabled returns screen rect.
void worldViewGetVisibleRect(Rect* rect);

// Returns size of the part of the world visible on screen at current zoom.
void worldViewGetVisibleSize(int* width, int* height);

// Converts between screen coordinates and world buffer coordinates. When world
// view is disabled these are identity transforms.
void worldViewScreenToWorld(int screenX, int screenY, int* worldX, int* worldY);
void worldViewWorldToScreen(int worldX, int worldY, int* screenX, int* screenY);

float worldViewGetZoom();

// Sets zoom keeping the world point under the anchor (in screen coordinates)
// at the same place on screen.
void worldViewSetZoom(float zoom, float anchorX, float anchorY);
void worldViewZoomBy(float factor, float anchorX, float anchorY);

// Moves the view by the given distance in screen pixels. Moving content to
// the right (positive dx) reveals the part of the map to the left.
void worldViewPanBy(float dx, float dy);

// Called by the tile engine when the center tile changes. Resets sub-tile pan
// offset unless the change was caused by world view panning itself.
void worldViewHandleCenterChanged();

// Sets map object which is drawn on top of the map without zooming (mouse
// cursor arrow, action menu). `anchorX` and `anchorY` is the point in world
// coordinates the object is attached to (mouse position). Pass `nullptr` to
// remove.
void worldViewSetOverlayObject(Object* object, int anchorX, int anchorY);
bool worldViewIsOverlayObject(Object* object);

// Scaling filter of the map: 0 - nearest, 1 - linear, 2 - sharp bilinear
// (`[world_view] filter`).
void worldViewSetFilter(int filter);

// The part of the map on screen as RGBA in the screen's palette now (with
// its brightness), at most [maxWidth] pixels wide. False when there's no map.
bool worldViewCaptureView(std::vector<unsigned char>* rgba, int* width, int* height, int maxWidth);

// Renders world layer. Must be called before UI texture is rendered.
void worldViewRender(SDL_Renderer* renderer);

// Releases GPU resources (renderer is about to be destroyed or was reset).
void worldViewResetRenderer();

} // namespace fallout

#endif /* FALLOUT_WORLD_VIEW_H_ */
