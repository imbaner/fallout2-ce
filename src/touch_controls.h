#ifndef FALLOUT_TOUCH_CONTROLS_H_
#define FALLOUT_TOUCH_CONTROLS_H_

#include "game_mouse.h"
#include "touch.h"

namespace fallout {

struct Object;

// CE: Touch-first controls on the map (enabled with `[touch] controls`).
//
// Instead of emulating mouse cursor, gestures over the map act directly:
//
// - tap on the ground: walk there (tap again on the same tile: run);
// - tap on an object: primary action (talk, use, pick up, loot);
// - long press on an object: radial action menu around the finger;
// - one finger drag: move the view, two fingers: move and zoom the view;
// - in combat first tap selects (shows hit chance / AP cost), second tap on
//   the same target or tile attacks / moves; tapping selected enemy again
//   attacks again.
//
// Map gestures call player commands with explicit object/tile targets. The
// game mouse and touch paths share those commands and the same game rules;
// neither a visible cursor nor a cursor warp is needed for map actions.
// With touch-native input there's no mouse at all (`touchControlsIsNative`).

bool touchControlsIsEnabled();

// Touch-native input: the mobile UI owns every screen and touch controls the
// map, so fingers reach the map from the gesture recognizer with no mouse in
// between (no cursor warp, no emulated clicks). Otherwise (the game's
// windows, `[touch] mobile_ui=0`) the mouse is emulated from touches for
// them.
bool touchControlsIsNative();

// Touch-native input: takes the recognizer's gestures (input's background
// processing, every frame, whatever the mouse's state): taps and long
// presses which waited longer than `kTouchStaleMs` (the game stalled) are
// dropped, the rest go to the map.
void touchControlsProcessGestures();

// Handles gesture, returns true if the gesture was consumed. One finger:
// taps, long presses, moving the view; two fingers: moving and zooming it.
bool touchControlsHandleGesture(const Gesture* gesture);

// Touch-native input: the last point touched on the map (screen pixels),
// what scripts reading the mouse get (see `mouseGetPointerPosition`). False
// before the first touch.
bool touchControlsGetLastPoint(int* x, int* y);

#if !FALLOUT_TOUCH_ONLY
// Mouse emulation for the game's windows: false if touch starting at the
// given point belongs to the map, so mouse cursor must not jump to the
// finger.
bool touchControlsWantsCursorWarp(int x, int y);

// Mouse emulation for the game's windows: adjusts point where finger touched
// the UI, small buttons nearby catch the touch.
void touchControlsAdjustUiTouch(int* x, int* y);
#endif

// Moves and zooms the map view keeping mouse cursor attached to the same
// point of the map.
void touchControlsMoveView(float dx, float dy, float zoomFactor, float anchorX, float anchorY);

// Closes radial menu and forgets combat selection.
void touchControlsReset();

// Shows action menu items (`GAME_MOUSE_ACTION_MENU_ITEM_*`) in radial menu
// around the point and waits for selection. Returns index of the selected
// item or -1 if the menu was cancelled.
// The combat selection waiting for confirmation (autotests): its action
// (TouchAction), tile and target.
void touchControlsGetSelection(int* action, int* tile, Object** target);

int touchControlsChooseActionMenuItem(const GameMouseActionMenuItem* items, int itemsLength, int x, int y);

// Returns true if hex cursor and cursor arrow should be visible: always
// without touch controls, with them only while selecting target in combat or
// for a skill/item.
bool touchControlsWantsGameMouseObjects();

// UI screens can take over one finger drags (e.g. world map scrolling). The
// handler is first called with `began` set and the point where the finger
// touched the screen, returning true takes the whole drag. Subsequent calls
// pass finger movement since the previous call.
typedef bool TouchControlsDragHandler(int x, int y, int dx, int dy, bool began);
void touchControlsSetDragHandler(TouchControlsDragHandler* handler);

// Offers one finger drag over UI to the registered drag handler, returns true
// if it was consumed.
bool touchControlsHandleUiDrag(const Gesture* gesture, int dx, int dy);

// Returns screen position of the radial menu button for the given action
// menu item (for automated tests).
bool touchControlsGetRadialMenuItemPosition(int menuItem, int* x, int* y);

} // namespace fallout

#endif /* FALLOUT_TOUCH_CONTROLS_H_ */
