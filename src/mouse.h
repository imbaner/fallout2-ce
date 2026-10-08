#ifndef FALLOUT_MOUSE_H_
#define FALLOUT_MOUSE_H_

#include "geometry.h"
#include "window.h"

namespace fallout {

#define MOUSE_DEFAULT_CURSOR_WIDTH 8
#define MOUSE_DEFAULT_CURSOR_HEIGHT 8
#define MOUSE_DEFAULT_CURSOR_SIZE (MOUSE_DEFAULT_CURSOR_WIDTH * MOUSE_DEFAULT_CURSOR_HEIGHT)

#define MOUSE_STATE_LEFT_BUTTON_DOWN 0x01
#define MOUSE_STATE_RIGHT_BUTTON_DOWN 0x02

#define MOUSE_EVENT_LEFT_BUTTON_DOWN 0x01
#define MOUSE_EVENT_RIGHT_BUTTON_DOWN 0x02
#define MOUSE_EVENT_LEFT_BUTTON_REPEAT 0x04
#define MOUSE_EVENT_RIGHT_BUTTON_REPEAT 0x08
#define MOUSE_EVENT_LEFT_BUTTON_UP 0x10
#define MOUSE_EVENT_RIGHT_BUTTON_UP 0x20
#define MOUSE_EVENT_ANY_BUTTON_DOWN (MOUSE_EVENT_LEFT_BUTTON_DOWN | MOUSE_EVENT_RIGHT_BUTTON_DOWN)
#define MOUSE_EVENT_ANY_BUTTON_REPEAT (MOUSE_EVENT_LEFT_BUTTON_REPEAT | MOUSE_EVENT_RIGHT_BUTTON_REPEAT)
#define MOUSE_EVENT_ANY_BUTTON_UP (MOUSE_EVENT_LEFT_BUTTON_UP | MOUSE_EVENT_RIGHT_BUTTON_UP)
#define MOUSE_EVENT_LEFT_BUTTON_DOWN_REPEAT (MOUSE_EVENT_LEFT_BUTTON_DOWN | MOUSE_EVENT_LEFT_BUTTON_REPEAT)
#define MOUSE_EVENT_RIGHT_BUTTON_DOWN_REPEAT (MOUSE_EVENT_RIGHT_BUTTON_DOWN | MOUSE_EVENT_RIGHT_BUTTON_REPEAT)
#define MOUSE_EVENT_WHEEL 0x40

#define BUTTON_REPEAT_TIME 250

#define MOUSE_SENSITIVITY_MIN 0.25
#define MOUSE_SENSITIVITY_MAX 2.5

extern WindowDrawingProc2* _mouse_blit_trans;
extern WINDOWDRAWINGPROC _mouse_blit;

int mouseInit();
void mouseFree();
int mouseSetFrame(unsigned char* frame, int width, int height, int pitch, int hotX, int hotY, char transparentColor);
void mouseShowCursor();
// CE: A touch gesture ended without its end (its finger was forgotten, see
// input.cc): no button stays held for it.
void mouseResetTouchGesture();
void mouseHideCursor();
void _mouse_info();
void _mouse_simulate_input(int delta_x, int delta_y, int buttons);
bool _mouse_in(int left, int top, int right, int bottom);
bool _mouse_click_in(int left, int top, int right, int bottom);
void mouseGetRect(Rect* rect);
void mouseGetPosition(int* out_x, int* out_y);
void _mouse_set_position(int x, int y);

// CE: With touch controls cursor is not drawn unless allowed (e.g. while
// dragging an item cursor shows the item).
void mouseSetTouchCursorVisible(bool visible);

// CE: Returns true if emulated mouse button was pressed by one finger drag /
// long press (touch controls).
bool mouseIsTouchDrag();
bool mouseIsTouchLongPress();
int mouseGetEvent();
bool cursorIsHidden();
void _mouse_get_raw_state(int* out_x, int* out_y, int* out_buttons);
void mouseSetSensitivity(double value);

void mouseGetPositionInWindow(int win, int* x, int* y);
bool mouseHitTestInWindow(int win, int left, int top, int right, int bottom);
void mouseGetWheel(int* x, int* y);
void convertMouseWheelToArrowKey(int* keyCodePtr);
int mouse_get_last_buttons();

// CE: Where the player points for scripts (sfall `get_mouse_x/y`,
// `tile_under_cursor`, `obj_under_cursor`): the mouse cursor, with
// touch-native input the last point touched on the map.
void mouseGetPointerPosition(int* x, int* y);
// CE: Buttons pressed for scripts (sfall `get_mouse_buttons`): the mouse's,
// with touch-native input the left one while a finger is on the map.
int mouseGetPointerButtons();

} // namespace fallout

#endif /* FALLOUT_MOUSE_H_ */
