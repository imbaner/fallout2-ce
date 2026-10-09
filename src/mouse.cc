#include "mouse.h"

#if __APPLE__
#include <TargetConditionals.h>
#endif

#include <vector>

#include "color.h"
#include "dinput.h"
#include "input.h"
#include "interface.h"
#include "kb.h"
#include "memory.h"
#include "platform/ios/quick_toolbar.h"
#include "svga.h"
#include "touch.h"
#include "touch_controls.h"
#include "window_manager.h"
#include "world_view.h"

namespace fallout {

static void mousePrepareDefaultCursor();
#if !FALLOUT_TOUCH_ONLY
static void _mouse_anim();
#endif
static void _mouse_clip();

// The default mouse cursor buffer.
//
// Initially it contains color codes, which will be replaced at startup
// according to loaded palette.
//
// Available color codes:
// - 0: transparent
// - 1: white
// - 15: black
//
// 0x51E250 or_mask
static unsigned char gMouseDefaultCursor[MOUSE_DEFAULT_CURSOR_SIZE] = {
    // clang-format off
    1,  1,  1,  1,  1,  1,  1, 0,
    1, 15, 15, 15, 15, 15,  1, 0,
    1, 15, 15, 15, 15,  1,  1, 0,
    1, 15, 15, 15, 15,  1,  1, 0,
    1, 15, 15, 15, 15, 15,  1, 1,
    1, 15,  1,  1, 15, 15, 15, 1,
    1,  1,  1,  1,  1, 15, 15, 1,
    0,  0,  0,  0,  1,  1,  1, 1,
    // clang-format on
};

// 0x51E290 mouse_idling
static int _mouse_idling = 0;

// 0x51E294 mouse_buf
static unsigned char* gMouseCursorData = nullptr;

// CE: Layer tags of `gMouseCursorData` pixels, see world_view.h.
static std::vector<unsigned char> gMouseCursorLayers;

#if !FALLOUT_TOUCH_ONLY
// CE: See `mouseSetTouchCursorVisible`.
static bool gTouchCursorVisible = false;

// CE: With touch controls one finger drag over the UI is emulated as mouse
// drag (left button held while moving).
static bool gTouchDragActive = false;

// CE: Frames left before the press of a tap on UI (see kTap below).
static int gTouchTapPending = 0;
static int gTouchTapPendingX = 0;
static int gTouchTapPendingY = 0;

// CE: Gesture which pressed emulated mouse button (see
// `mouseIsTouchDrag`).
static GestureType gTouchButtonGesture = kUnrecognized;
#endif

#if !FALLOUT_TOUCH_ONLY
// 0x51E298 mouse_shape
static unsigned char* _mouse_shape = nullptr;
#endif

#if !FALLOUT_TOUCH_ONLY
// 0x51E29C mouse_fptr
static unsigned char* _mouse_fptr = nullptr;
#endif

// 0x51E2A0 mouse_sensitivity
static double gMouseSensitivity = 1.0;

// 0x51E2AC last_buttons
static int last_buttons = 0;

// 0x6AC790 mouse_is_hidden
static bool gCursorIsHidden;

// 0x6AC794 raw_x
static int _raw_x;

// 0x6AC798 mouse_length
static int gMouseCursorHeight;

// 0x6AC79C raw_y
static int _raw_y;

// 0x6AC7A0 raw_buttons
static int _raw_buttons;

// 0x6AC7A4 mouse_y
static int gMouseCursorY;

// 0x6AC7A8 mouse_x
static int gMouseCursorX;

// 0x6AC7AC mouse_disabled
static int _mouse_disabled;

// 0x6AC7B0 mouse_buttons
static int gMouseEvent;

#if !FALLOUT_TOUCH_ONLY
// 0x6AC7B4 mouse_speed
static unsigned int _mouse_speed;
#endif

#if !FALLOUT_TOUCH_ONLY
// 0x6AC7B8 mouse_curr_frame
static int _mouse_curr_frame;
#endif

// 0x6AC7BC have_mouse
static bool gMouseInitialized;

#if !FALLOUT_TOUCH_ONLY
// 0x6AC7C0 mouse_pitch
static int gMouseCursorPitch;
#endif

// 0x6AC7C4 mouse_width
static int gMouseCursorWidth;

#if !FALLOUT_TOUCH_ONLY
// 0x6AC7C8 mouse_num_frames
static int _mouse_num_frames;
#endif

// 0x6AC7CC mouse_hoty
static int _mouse_hoty;

// 0x6AC7D0 mouse_hotx
static int _mouse_hotx;

// 0x6AC7D4 mouse_idle_start_time
static unsigned int _mouse_idle_start_time;

// 0x6AC7D8 mouse_blit_trans
WindowDrawingProc2* _mouse_blit_trans;

// 0x6AC7DC mouse_blit
WINDOWDRAWINGPROC _mouse_blit;

#if !FALLOUT_TOUCH_ONLY
// 0x6AC7E0 mouse_trans
static char _mouse_trans;
#endif

static int gMouseWheelX = 0;
static int gMouseWheelY = 0;

// 0x4C9F40
int mouseInit()
{
    gMouseInitialized = false;
    _mouse_disabled = 0;

    gCursorIsHidden = true;

    mousePrepareDefaultCursor();

    if (mouseSetFrame(nullptr, 0, 0, 0, 0, 0, 0) == -1) {
        return -1;
    }

    if (!mouseDeviceAcquire()) {
        return -1;
    }

    gMouseInitialized = true;
    gMouseCursorX = _scr_size.right / 2;
    gMouseCursorY = _scr_size.bottom / 2;
    _raw_x = _scr_size.right / 2;
    _raw_y = _scr_size.bottom / 2;
    _mouse_idle_start_time = getTicks();

    return 0;
}

// 0x4C9FD8
void mouseFree()
{
    mouseDeviceUnacquire();

    if (gMouseCursorData != nullptr) {
        internal_free(gMouseCursorData);
        gMouseCursorData = nullptr;
    }

#if !FALLOUT_TOUCH_ONLY
    if (_mouse_fptr != nullptr) {
        tickersRemove(_mouse_anim);
        _mouse_fptr = nullptr;
    }
#endif
}

// 0x4CA01C
static void mousePrepareDefaultCursor()
{
    for (int index = 0; index < 64; index++) {
        switch (gMouseDefaultCursor[index]) {
        case 0:
            gMouseDefaultCursor[index] = COLOR_BLACK;
            break;
        case 1:
            gMouseDefaultCursor[index] = COLOR_DARK_GREY;
            break;
        case 15:
            gMouseDefaultCursor[index] = COLOR_WHITE;
            break;
        }
    }
}

#if FALLOUT_TOUCH_ONLY
// CE: No cursor in the touch-only build (touch.h): its art isn't kept and
// nothing is drawn, the game only knows whether it's shown.
int mouseSetFrame(unsigned char* frame, int width, int height, int pitch, int hotX, int hotY, char transparentColor)
{
    return 0;
}

void mouseShowCursor()
{
    if (gMouseInitialized) {
        gCursorIsHidden = false;
    }
}

void mouseHideCursor()
{
    if (gMouseInitialized) {
        gCursorIsHidden = true;
    }
}
#else
// 0x4CA0AC
int mouseSetFrame(unsigned char* frame, int width, int height, int pitch, int hotX, int hotY, char transparentColor)
{
    Rect rect;
    unsigned char* cursorFrame;
    int hotXDelta;
    int hotYDelta;

    cursorFrame = frame;

    if (frame == nullptr) {
        // NOTE: Original code looks tail recursion optimization.
        return mouseSetFrame(gMouseDefaultCursor, MOUSE_DEFAULT_CURSOR_WIDTH, MOUSE_DEFAULT_CURSOR_HEIGHT, MOUSE_DEFAULT_CURSOR_WIDTH, 1, 1, COLOR_BLACK);
    }

    bool cursorWasHidden = gCursorIsHidden;
    if (!gCursorIsHidden && gMouseInitialized) {
        gCursorIsHidden = true;
        mouseGetRect(&rect);
        windowRefreshAll(&rect);
    }

    if (width != gMouseCursorWidth || height != gMouseCursorHeight) {
        unsigned char* buf = (unsigned char*)internal_malloc(static_cast<size_t>(width) * height);
        if (buf == nullptr) {
            if (!cursorWasHidden) {
                mouseShowCursor();
            }
            return -1;
        }

        if (gMouseCursorData != nullptr) {
            internal_free(gMouseCursorData);
        }

        gMouseCursorData = buf;
    }

    gMouseCursorWidth = width;
    gMouseCursorHeight = height;
    gMouseCursorPitch = pitch;
    _mouse_shape = cursorFrame;
    _mouse_trans = transparentColor;

    if (_mouse_fptr) {
        tickersRemove(_mouse_anim);
        _mouse_fptr = nullptr;
    }

    hotXDelta = _mouse_hotx - hotX;
    _mouse_hotx = hotX;

    gMouseCursorX += hotXDelta;

    hotYDelta = _mouse_hoty - hotY;
    _mouse_hoty = hotY;

    gMouseCursorY += hotYDelta;

    _mouse_clip();

    if (!cursorWasHidden) {
        mouseShowCursor();
    }

    _raw_x = gMouseCursorX;
    _raw_y = gMouseCursorY;

    return 0;
}

// NOTE: Looks like this code is not reachable.
//
// 0x4CA2D0
static void _mouse_anim()
{
    // 0x51E2A8
    static unsigned int ticker = 0;

    if (getTicksSince(ticker) >= _mouse_speed) {
        ticker = getTicks();

        if (++_mouse_curr_frame == _mouse_num_frames) {
            _mouse_curr_frame = 0;
        }

        _mouse_shape = gMouseCursorWidth * _mouse_curr_frame * gMouseCursorHeight + _mouse_fptr;

        if (!gCursorIsHidden) {
            mouseShowCursor();
        }
    }
}

// 0x4CA34C
void mouseShowCursor()
{
    unsigned char* cursorData;
    int clipX;
    int clipWidth;
    int clipY;
    int clipHeight;
    int cursorDataIndex;

    cursorData = gMouseCursorData;
    if (gMouseInitialized) {
        gMouseCursorLayers.resize(static_cast<size_t>(gMouseCursorWidth) * gMouseCursorHeight, kScreenLayerUi);

        if (!_mouse_blit_trans || !gCursorIsHidden) {
            _win_get_mouse_buf(gMouseCursorData, gMouseCursorLayers.data());
            cursorData = gMouseCursorData;
            cursorDataIndex = 0;

            // CE: With touch controls there is no cursor, except when an item
            // is dragged (cursor shows dragged item).
#if FALLOUT_TOUCH_ONLY
            // CE: No cursor in the touch-only build.
            bool drawCursorShape = false;
#else
            bool drawCursorShape = !touchControlsIsEnabled() || gTouchCursorVisible;
#endif

            for (int y = 0; y < gMouseCursorHeight && drawCursorShape; y++) {
                for (int x = 0; x < gMouseCursorWidth; x++) {
                    unsigned char pixel = _mouse_shape[y * gMouseCursorPitch + x];
                    if (pixel != _mouse_trans) {
                        cursorData[cursorDataIndex] = pixel;
                        gMouseCursorLayers[cursorDataIndex] = kScreenLayerUi;
                    }
                    cursorDataIndex++;
                }
            }
        }

        if (gMouseCursorX >= _scr_size.left) {
            if (gMouseCursorWidth + gMouseCursorX - 1 <= _scr_size.right) {
                clipWidth = gMouseCursorWidth;
                clipX = 0;
            } else {
                clipX = 0;
                clipWidth = _scr_size.right - gMouseCursorX + 1;
            }
        } else {
            clipX = _scr_size.left - gMouseCursorX;
            clipWidth = gMouseCursorWidth - (_scr_size.left - gMouseCursorX);
        }

        if (gMouseCursorY >= _scr_size.top) {
            if (gMouseCursorHeight + gMouseCursorY - 1 <= _scr_size.bottom) {
                clipY = 0;
                clipHeight = gMouseCursorHeight;
            } else {
                clipY = 0;
                clipHeight = _scr_size.bottom - gMouseCursorY + 1;
            }
        } else {
            clipY = _scr_size.top - gMouseCursorY;
            clipHeight = gMouseCursorHeight - (_scr_size.top - gMouseCursorY);
        }

        gMouseCursorData = cursorData;
        if (_mouse_blit_trans && gCursorIsHidden) {
            _mouse_blit_trans(_mouse_shape, gMouseCursorPitch, gMouseCursorHeight, clipX, clipY, clipWidth, clipHeight, clipX + gMouseCursorX, clipY + gMouseCursorY, _mouse_trans);
        } else {
            screenLayersSetBlitSource(gMouseCursorLayers.data());
            _mouse_blit(gMouseCursorData, gMouseCursorWidth, gMouseCursorHeight, clipX, clipY, clipWidth, clipHeight, clipX + gMouseCursorX, clipY + gMouseCursorY);
            screenLayersSetBlitSource(nullptr);
        }

        cursorData = gMouseCursorData;
        gCursorIsHidden = false;
    }
    gMouseCursorData = cursorData;
}

// 0x4CA534
void mouseHideCursor()
{
    Rect rect;

    if (gMouseInitialized) {
        if (!gCursorIsHidden) {
            rect.left = gMouseCursorX;
            rect.top = gMouseCursorY;
            rect.right = gMouseCursorX + gMouseCursorWidth - 1;
            rect.bottom = gMouseCursorY + gMouseCursorHeight - 1;

            gCursorIsHidden = true;
            windowRefreshAll(&rect);
        }
    }
}
#endif

#if FALLOUT_TOUCH_ONLY
void mouseResetTouchGesture()
{
}
#else
#if __APPLE__ && TARGET_OS_IOS
// Checks whether a tap lands on an interface-bar button and, if so,
// injects the corresponding keyCode so the cursor never moves.
// Returns true if the tap was consumed (button injected or bare chrome hit).
static bool handleHudTapThrough(const Gesture& gesture)
{
    if (!mouseDeviceUsesRelativeMode() || touch_get_touchscreen_mode() || gInterfaceBarWindow == -1) {
        return false;
    }

    Window* hudWindow = windowGetWindow(gInterfaceBarWindow);
    if (hudWindow == nullptr || (hudWindow->flags & WINDOW_HIDDEN) != 0) {
        return false;
    }

    Rect hudRect;
    if (windowGetRect(gInterfaceBarWindow, &hudRect) != 0
        || gesture.x < hudRect.left || gesture.x > hudRect.right
        || gesture.y < hudRect.top || gesture.y > hudRect.bottom) {
        return false;
    }

    if (gesture.numberOfTouches == 1 || gesture.numberOfTouches == 2) {
        for (Button* button = hudWindow->buttonListHead; button != nullptr; button = button->next) {
            if ((button->flags & BUTTON_FLAG_DISABLED) != 0) {
                continue;
            }
            int left = hudWindow->rect.left + button->rect.left;
            int top = hudWindow->rect.top + button->rect.top;
            int right = hudWindow->rect.left + button->rect.right;
            int bottom = hudWindow->rect.top + button->rect.bottom;
            if (gesture.x < left || gesture.x > right || gesture.y < top || gesture.y > bottom) {
                continue;
            }
            int keyCode = gesture.numberOfTouches == 1
                ? button->leftMouseUpEventCode
                : button->rightMouseUpEventCode;
            if (keyCode == -1) {
                break;
            }
            enqueueInputEvent(keyCode);
            return true;
        }
    }

    // Tap landed on belt chrome (no button under it). Consume silently
    // rather than teleporting the cursor to an inert region.
    return true;
}
#endif

void mouseResetTouchGesture()
{
    bool held = gTouchDragActive || gTouchButtonGesture != kUnrecognized;
    gTouchDragActive = false;
    gTouchButtonGesture = kUnrecognized;
    if (held) {
        _mouse_simulate_input(0, 0, 0);
    }
}

// CE: Mouse emulated from touches for the game's windows (the legacy UI,
// `touchControlsIsNative` false): a tap moves the cursor and clicks, a long
// press holds a button (or right clicks), a one finger drag drags or scrolls,
// multi-finger shortcuts; map gestures still go to touch controls first.
// Returns true when the mouse's state for this call is set.
static bool mouseEmulateTouch()
{
    // CE: Gestures made meanwhile would be handled all at once later (taps
    // long after they were made): they're dropped, a held button let go.
    if (gCursorIsHidden || _mouse_disabled) {
        Gesture dropped;
        bool any = false;
        while (touch_get_gesture(&dropped)) {
            any = true;
        }
        if (any) {
            mouseResetTouchGesture();
        }
        return true;
    }

    // CE: Gestures handled by the map (touch controls, view movement) are
    // processed all at once, so the view keeps up with fingers. Mouse
    // emulation still gets one gesture per call.
    Gesture gesture;
    // CE: Press of a tap on UI is delayed (see kTap below).
    if (gTouchTapPending > 0) {
        gTouchTapPending--;
        if (gTouchTapPending > 0) {
            // Cursor stays, buttons up.
            if (mouseDeviceUsesRelativeMode()) {
                _mouse_simulate_input(0, 0, 0);
            } else {
                _mouse_simulate_input(gTouchTapPendingX, gTouchTapPendingY, 0);
            }
            return true;
        }

        gTouchButtonGesture = kTap;
        if (mouseDeviceUsesRelativeMode()) {
            _mouse_simulate_input(0, 0, MOUSE_STATE_LEFT_BUTTON_DOWN);
        } else {
            _mouse_simulate_input(gTouchTapPendingX, gTouchTapPendingY, MOUSE_STATE_LEFT_BUTTON_DOWN);
        }
        return true;
    }

    bool hadGesture = false;
    while (touch_get_gesture(&gesture)) {
        hadGesture = true;

        static int prevx;
        static int prevy;

        // CE: Touch controls have no hidden multi-finger gestures: two finger
        // tap/long press (right mouse button) and three/four finger
        // shortcuts are ignored (two finger pan moves the map).
        if (touchControlsIsEnabled()
            && gesture.numberOfTouches >= 2
            && !(gesture.type == kPan && gesture.numberOfTouches == 2)) {
            continue;
        }

        // Multi-finger gestures for keyboard-less touch play:
        //   3-finger swipe down → ESC (options menu)
        //   3-finger long press → hold Left Shift (highlights interactables)
        //   4-finger long press → F6  (quicksave)
        if (gesture.type == kPan && gesture.numberOfTouches == 3) {
            static int swipeStartY;
            if (gesture.state == kBegan) {
                swipeStartY = gesture.y;
            } else if (gesture.state == kEnded) {
                int dy = gesture.y - swipeStartY;
                if (dy > screenGetHeight() / 4) {
                    enqueueInputEvent(KEY_ESCAPE);
                }
            }
            return true;
        }

        // Four-finger long press → F6 (quicksave). Long-press is more
        // reliable than a tap since all 4 fingers rarely land and lift
        // within the 75ms tap window; and more reliable than a swipe
        // since iPadOS intercepts multi-finger vertical swipes.
        if (gesture.type == kLongPress && gesture.numberOfTouches == 4) {
            if (gesture.state == kBegan) {
                enqueueInputEvent(KEY_F6);
            }
            return true;
        }

        // FO2tweaks' highlighting uses sfall's key_pressed(), which reads
        // SDL's own SDL_GetKeyboardState. Engine-internal _kb_simulate_key
        // bypasses that, so push real SDL_KEYDOWN/UP events instead.
        if (gesture.type == kLongPress && gesture.numberOfTouches == 3) {
            static bool shiftHeld = false;
            SDL_Event ev;
            SDL_zero(ev);
            ev.key.keysym.scancode = SDL_SCANCODE_LSHIFT;
            ev.key.keysym.sym = SDLK_LSHIFT;
            if (gesture.state == kBegan && !shiftHeld) {
                ev.type = SDL_KEYDOWN;
                ev.key.state = SDL_PRESSED;
                SDL_PushEvent(&ev);
                shiftHeld = true;
            } else if (gesture.state == kEnded && shiftHeld) {
                ev.type = SDL_KEYUP;
                ev.key.state = SDL_RELEASED;
                SDL_PushEvent(&ev);
                shiftHeld = false;
            }
            return true;
        }

        // CE: Touch-first controls on the map (two fingers: its view).
        if (touchControlsHandleGesture(&gesture)) {
            continue;
        }

        switch (gesture.type) {
        case kTap: {
            // Toolbar taps bypass the mouse pipeline entirely: the handler
            // invokes the action in place, so the cursor never moves.
            // Skip when touchscreen mode is active (dialog, inventory, etc.)
            // so toolbar doesn't intercept taps meant for overlapping UI.
            if (!touch_get_touchscreen_mode()
                && gesture.numberOfTouches == 1
                && quickToolbarContainsPoint(gesture.x, gesture.y)) {
                if (quickToolbarHandleTap(gesture.x, gesture.y)) {
                    break;
                }
            }

#if __APPLE__ && TARGET_OS_IOS
            if (handleHudTapThrough(gesture)) {
                goto tap_done;
            }
#endif

            gTouchButtonGesture = kTap;

            // CE: Window manager needs a frame to leave the button the cursor
            // was over and a frame to enter the new one before it takes a
            // press. A quick tap (finger up in the same frame as down) moved
            // the cursor and pressed at once, so the press was lost (e.g.
            // attack mode chips kept the previous mode). Move the cursor now,
            // press two frames later (no gesture handling meanwhile).
            if (touchControlsIsEnabled() && gesture.numberOfTouches == 1 && touchControlsWantsCursorWarp(gesture.x, gesture.y)) {
                int x = gesture.x;
                int y = gesture.y;
                touchControlsAdjustUiTouch(&x, &y);

                mouseHideCursor();
                _mouse_set_position(x, y);
                mouseShowCursor();

                gTouchTapPending = 2;
                gTouchTapPendingX = x;
                gTouchTapPendingY = y;

                // Cursor moved with buttons up this frame.
                if (mouseDeviceUsesRelativeMode()) {
                    _mouse_simulate_input(0, 0, 0);
                } else {
                    _mouse_simulate_input(x, y, 0);
                }
                break;
            }

            if (mouseDeviceUsesRelativeMode()) {
                if (gesture.numberOfTouches == 1) {
                    _mouse_simulate_input(0, 0, MOUSE_STATE_LEFT_BUTTON_DOWN);
                } else if (gesture.numberOfTouches == 2) {
                    _mouse_simulate_input(0, 0, MOUSE_STATE_RIGHT_BUTTON_DOWN);
                }
            } else {
                _mouse_set_position(gesture.x, gesture.y);
                if (gesture.numberOfTouches == 1) {
                    _mouse_simulate_input(gesture.x, gesture.y, MOUSE_STATE_LEFT_BUTTON_DOWN);
                } else if (gesture.numberOfTouches == 2) {
                    _mouse_simulate_input(gesture.x, gesture.y, MOUSE_STATE_RIGHT_BUTTON_DOWN);
                }
            }
        tap_done:
            break;
        }
        case kLongPress:
        case kPan:
            if (gesture.state == kBegan) {
                prevx = gesture.x;
                prevy = gesture.y;
            }
            if (!mouseDeviceUsesRelativeMode()) {
                prevx = 0;
                prevy = 0;
            }

            if (gesture.type == kLongPress) {
                gTouchButtonGesture = kLongPress;

                // CE: With touch controls long press on a button with right
                // click action is its right click (e.g. weapon button cycles
                // attack modes: single, aimed, burst).
                static bool touchLongPressIsRightClick = false;
                if (gesture.state == kBegan) {
                    touchLongPressIsRightClick = touchControlsIsEnabled()
                        && gesture.numberOfTouches == 1
                        && windowButtonAtPointHasRightClick(gesture.startX, gesture.startY);
                }

                if (touchLongPressIsRightClick) {
                    _mouse_simulate_input(gesture.x - prevx, gesture.y - prevy, MOUSE_STATE_RIGHT_BUTTON_DOWN);
                } else if (gesture.numberOfTouches == 1) {
                    _mouse_simulate_input(gesture.x - prevx, gesture.y - prevy, MOUSE_STATE_LEFT_BUTTON_DOWN);
                } else if (gesture.numberOfTouches == 2) {
                    _mouse_simulate_input(gesture.x - prevx, gesture.y - prevy, MOUSE_STATE_RIGHT_BUTTON_DOWN);
                }
            } else if (gesture.type == kPan) {
                // CE: With touch controls nearly vertical one finger swipes
                // over UI scroll lists (natural direction: content follows
                // the finger), other directions drag things (items,
                // sliders). Two fingers do nothing over UI.
                bool touchDrag = false;
                bool touchScroll = false;
                if (gesture.numberOfTouches == 1 && touchControlsHandleUiDrag(&gesture, gesture.x - prevx, gesture.y - prevy)) {
                    // Taken over by the screen (e.g. world map scrolling).
                    touchScroll = true;
                } else if (touchControlsIsEnabled()) {
                    static bool touchScrollGesture = false;
                    static int touchScrollRemainder = 0;
                    if (gesture.numberOfTouches == 1) {
                        if (gesture.state == kBegan) {
                            // Dragging an item towards hands and other lists
                            // is diagonal.
                            touchScrollGesture = abs(gesture.x - gesture.startX) * 2 < abs(gesture.y - gesture.startY);
                            touchScrollRemainder = gesture.y - gesture.startY;
                        } else {
                            touchScrollRemainder += gesture.y - prevy;
                        }
                        touchDrag = !touchScrollGesture;
                        touchScroll = touchScrollGesture;
                    }

                    if (touchScroll) {
                        // One list line per this many pixels of finger
                        // movement.
                        constexpr int kScrollStep = 20;
                        int lines = touchScrollRemainder / kScrollStep;
                        touchScrollRemainder -= lines * kScrollStep;
                        if (lines != 0) {
                            gMouseWheelX = 0;
                            gMouseWheelY = lines;
                            gMouseEvent |= MOUSE_EVENT_WHEEL;
                            _raw_buttons |= MOUSE_EVENT_WHEEL;
                        }
                    }
                }

                if (touchScroll || (touchControlsIsEnabled() && gesture.numberOfTouches != 1)) {
                    // Handled above / ignored.
                } else if (touchDrag) {
                    gTouchButtonGesture = kPan;
                    gTouchDragActive = gesture.state != kEnded;
                    _mouse_simulate_input(gesture.x - prevx, gesture.y - prevy, MOUSE_STATE_LEFT_BUTTON_DOWN);
                } else if (!touch_get_pan_mode() && gesture.numberOfTouches == 1) {
                    _mouse_simulate_input(gesture.x - prevx, gesture.y - prevy, 0);
                } else if (touch_get_pan_mode() || gesture.numberOfTouches == 2) {
                    int coefficient = touch_get_pan_mode() ? 8 : 2;
                    gMouseWheelX = (prevx - gesture.x) / coefficient;
                    gMouseWheelY = (gesture.y - prevy) / coefficient;

                    if (gMouseWheelX != 0 || gMouseWheelY != 0) {
                        gMouseEvent |= MOUSE_EVENT_WHEEL;
                        _raw_buttons |= MOUSE_EVENT_WHEEL;
                    }
                }
            }

            prevx = gesture.x;
            prevy = gesture.y;
            break;
        case kUnrecognized:
            break;
        }

        return true;
    }

    if (hadGesture) {
        return true;
    }

    // CE: Keep button held between drag events.
    if (gTouchDragActive) {
        _mouse_simulate_input(0, 0, MOUSE_STATE_LEFT_BUTTON_DOWN);
        return true;
    }

    return false;
}
#endif

// 0x4CA59C
void _mouse_info()
{
    if (!gMouseInitialized) {
        return;
    }

#if FALLOUT_TOUCH_ONLY
    // CE: No mouse: fingers' gestures are the map's
    // (`touchControlsProcessGestures`), a connected mouse is ignored.
    return;
#else
    // CE: Touch-native input: fingers' gestures are the map's
    // (`touchControlsProcessGestures`), the mouse is only a real one.
    if (!touchControlsIsNative() && mouseEmulateTouch()) {
        return;
    }
#endif

    if (gCursorIsHidden || _mouse_disabled) {
        return;
    }

    int x;
    int y;
    int buttons = 0;

    MouseData mouseData;
    if (mouseDeviceGetData(&mouseData)) {
        x = mouseData.x;
        y = mouseData.y;

        if (mouseData.buttons[0] == 1) {
            buttons |= MOUSE_STATE_LEFT_BUTTON_DOWN;
        }

        if (mouseData.buttons[1] == 1) {
            buttons |= MOUSE_STATE_RIGHT_BUTTON_DOWN;
        }
    } else {
        x = 0;
        y = 0;
    }

    // Mouse sensitivity only applies to relative movement. In windowed mode
    // SDL provides absolute coordinates that should not be scaled.
    if (mouseDeviceUsesRelativeMode()) {
        x = (int)(x * gMouseSensitivity);
        y = (int)(y * gMouseSensitivity);
    }

    _mouse_simulate_input(x, y, buttons);

    // TODO: Move to `_mouse_simulate_input`.
    gMouseWheelX = mouseData.wheelX;
    gMouseWheelY = mouseData.wheelY;

    if (gMouseWheelX != 0 || gMouseWheelY != 0) {
        gMouseEvent |= MOUSE_EVENT_WHEEL;
        _raw_buttons |= MOUSE_EVENT_WHEEL;
    }
}

#if FALLOUT_TOUCH_ONLY
void mouseSetTouchCursorVisible(bool visible)
{
}

bool mouseIsTouchDrag()
{
    return false;
}

bool mouseIsTouchLongPress()
{
    return false;
}
#else
void mouseSetTouchCursorVisible(bool visible)
{
    gTouchCursorVisible = visible;
}

bool mouseIsTouchDrag()
{
    return gTouchButtonGesture == kPan;
}

bool mouseIsTouchLongPress()
{
    return gTouchButtonGesture == kLongPress;
}
#endif

// 0x4CA698
void _mouse_simulate_input(int delta_x, int delta_y, int buttons)
{
    // 0x6AC7E4
    static unsigned int previousRightButtonTimestamp;

    // 0x6AC7E8
    static unsigned int previousLeftButtonTimestamp;

    // 0x6AC7EC
    static int previousEvent;

    if (!gMouseInitialized || gCursorIsHidden) {
        return;
    }

    if (delta_x == 0 && delta_y == 0 && buttons == last_buttons) {
        if (last_buttons == 0) {
            if (!_mouse_idling) {
                _mouse_idle_start_time = getTicks();
                _mouse_idling = 1;
            }

            last_buttons = 0;
            _raw_buttons = 0;
            gMouseEvent = 0;

            return;
        }
    }

    _mouse_idling = 0;
    last_buttons = buttons;
    previousEvent = gMouseEvent;
    gMouseEvent = 0;

    if ((previousEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN_REPEAT) != 0) {
        if ((buttons & 0x01) != 0) {
            gMouseEvent |= MOUSE_EVENT_LEFT_BUTTON_REPEAT;

            if (getTicksSince(previousLeftButtonTimestamp) > BUTTON_REPEAT_TIME) {
                gMouseEvent |= MOUSE_EVENT_LEFT_BUTTON_DOWN;
                previousLeftButtonTimestamp = getTicks();
            }
        } else {
            gMouseEvent |= MOUSE_EVENT_LEFT_BUTTON_UP;
        }
    } else {
        if ((buttons & 0x01) != 0) {
            gMouseEvent |= MOUSE_EVENT_LEFT_BUTTON_DOWN;
            previousLeftButtonTimestamp = getTicks();
        }
    }

    if ((previousEvent & MOUSE_EVENT_RIGHT_BUTTON_DOWN_REPEAT) != 0) {
        if ((buttons & 0x02) != 0) {
            gMouseEvent |= MOUSE_EVENT_RIGHT_BUTTON_REPEAT;
            if (getTicksSince(previousRightButtonTimestamp) > BUTTON_REPEAT_TIME) {
                gMouseEvent |= MOUSE_EVENT_RIGHT_BUTTON_DOWN;
                previousRightButtonTimestamp = getTicks();
            }
        } else {
            gMouseEvent |= MOUSE_EVENT_RIGHT_BUTTON_UP;
        }
    } else {
        if (buttons & 0x02) {
            gMouseEvent |= MOUSE_EVENT_RIGHT_BUTTON_DOWN;
            previousRightButtonTimestamp = getTicks();
        }
    }

    _raw_buttons = gMouseEvent;

    if (delta_x != 0 || delta_y != 0) {
        Rect mouseRect;
        mouseRect.left = gMouseCursorX;
        mouseRect.top = gMouseCursorY;
        mouseRect.right = gMouseCursorWidth + gMouseCursorX - 1;
        mouseRect.bottom = gMouseCursorHeight + gMouseCursorY - 1;
        if (mouseDeviceUsesRelativeMode()) {
            gMouseCursorX += delta_x;
            gMouseCursorY += delta_y;
        } else {
            _mouse_set_position(delta_x, delta_y);
        }
        _mouse_clip();

        windowRefreshAll(&mouseRect);

        mouseShowCursor();

        if (mouseDeviceUsesRelativeMode()) {
            _raw_x = gMouseCursorX;
            _raw_y = gMouseCursorY;
        } else {
            _raw_x = delta_x;
            _raw_y = delta_y;
        }
    }
}

// 0x4CA8C8
bool _mouse_in(int left, int top, int right, int bottom)
{
    if (!gMouseInitialized) {
        return false;
    }

    return gMouseCursorHeight + gMouseCursorY > top
        && right >= gMouseCursorX
        && gMouseCursorWidth + gMouseCursorX > left
        && bottom >= gMouseCursorY;
}

// 0x4CA934
bool _mouse_click_in(int left, int top, int right, int bottom)
{
    if (!gMouseInitialized) {
        return false;
    }

    return _mouse_hoty + gMouseCursorY >= top
        && _mouse_hotx + gMouseCursorX <= right
        && _mouse_hotx + gMouseCursorX >= left
        && _mouse_hoty + gMouseCursorY <= bottom;
}

// 0x4CA9A0
void mouseGetRect(Rect* rect)
{
    rect->left = gMouseCursorX;
    rect->top = gMouseCursorY;
    rect->right = gMouseCursorWidth + gMouseCursorX - 1;
    rect->bottom = gMouseCursorHeight + gMouseCursorY - 1;
}

// 0x4CA9DC
void mouseGetPosition(int* xPtr, int* yPtr)
{
    *xPtr = _mouse_hotx + gMouseCursorX;
    *yPtr = _mouse_hoty + gMouseCursorY;
}

// 0x4CAA04
void _mouse_set_position(int x, int y)
{
    gMouseCursorX = x - _mouse_hotx;
    gMouseCursorY = y - _mouse_hoty;
    _raw_y = y - _mouse_hoty;
    _raw_x = x - _mouse_hotx;
    _mouse_clip();
}

// 0x4CAA38
static void _mouse_clip()
{
    if (_mouse_hotx + gMouseCursorX < _scr_size.left) {
        gMouseCursorX = _scr_size.left - _mouse_hotx;
    } else if (_mouse_hotx + gMouseCursorX > _scr_size.right) {
        gMouseCursorX = _scr_size.right - _mouse_hotx;
    }

    if (_mouse_hoty + gMouseCursorY < _scr_size.top) {
        gMouseCursorY = _scr_size.top - _mouse_hoty;
    } else if (_mouse_hoty + gMouseCursorY > _scr_size.bottom) {
        gMouseCursorY = _scr_size.bottom - _mouse_hoty;
    }
}

// 0x4CAAA0
int mouseGetEvent()
{
    return gMouseEvent;
}

// 0x4CAAA8
bool cursorIsHidden()
{
    return gCursorIsHidden;
}

// 0x4CAB5C
void _mouse_get_raw_state(int* out_x, int* out_y, int* out_buttons)
{
    MouseData mouseData;
    if (!mouseDeviceGetData(&mouseData)) {
        mouseData.x = 0;
        mouseData.y = 0;
        mouseData.buttons[0] = (gMouseEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0;
        mouseData.buttons[1] = (gMouseEvent & MOUSE_EVENT_RIGHT_BUTTON_DOWN) != 0;
    }

    _raw_buttons = 0;
    if (mouseDeviceUsesRelativeMode()) {
        _raw_x += mouseData.x;
        _raw_y += mouseData.y;
    } else {
        _raw_x = mouseData.x;
        _raw_y = mouseData.y;
    }

    if (mouseData.buttons[0] != 0) {
        _raw_buttons |= MOUSE_EVENT_LEFT_BUTTON_DOWN;
    }

    if (mouseData.buttons[1] != 0) {
        _raw_buttons |= MOUSE_EVENT_RIGHT_BUTTON_DOWN;
    }

    *out_x = _raw_x;
    *out_y = _raw_y;
    *out_buttons = _raw_buttons;
}

// 0x4CAC3C
void mouseSetSensitivity(double value)
{
    if (value >= MOUSE_SENSITIVITY_MIN && value <= MOUSE_SENSITIVITY_MAX) {
        gMouseSensitivity = value;
    }
}

void mouseGetPositionInWindow(int win, int* x, int* y)
{
    mouseGetPosition(x, y);

    Window* window = windowGetWindow(win);
    if (window != nullptr) {
        *x -= window->rect.left;
        *y -= window->rect.top;
    }
}

bool mouseHitTestInWindow(int win, int left, int top, int right, int bottom)
{
    Window* window = windowGetWindow(win);
    if (window != nullptr) {
        left += window->rect.left;
        top += window->rect.top;
        right += window->rect.left;
        bottom += window->rect.top;
    }

    return _mouse_click_in(left, top, right, bottom);
}

void mouseGetWheel(int* x, int* y)
{
    *x = gMouseWheelX;
    *y = gMouseWheelY;
}

void convertMouseWheelToArrowKey(int* keyCodePtr)
{
    if (*keyCodePtr == -1) {
        if ((mouseGetEvent() & MOUSE_EVENT_WHEEL) != 0) {
            int wheelX;
            int wheelY;
            mouseGetWheel(&wheelX, &wheelY);

            if (wheelY > 0) {
                *keyCodePtr = KEY_ARROW_UP;
            } else if (wheelY < 0) {
                *keyCodePtr = KEY_ARROW_DOWN;
            }
        }
    }
}

int mouse_get_last_buttons()
{
    return last_buttons;
}

int mouseGetPointerButtons()
{
    // CE: Touch-native input: the left button while a finger is on the map.
    if (touchControlsIsNative()) {
        return touch_any_finger_down() ? MOUSE_STATE_LEFT_BUTTON_DOWN : 0;
    }

    return last_buttons;
}

void mouseGetPointerPosition(int* x, int* y)
{
    // CE: Touch-native input has no cursor: the last point touched on the
    // map (the screen's center before the first touch in the touch-only
    // build).
    if (touchControlsIsNative() && touchControlsGetLastPoint(x, y)) {
        return;
    }

#if FALLOUT_TOUCH_ONLY
    *x = screenGetWidth() / 2;
    *y = screenGetHeight() / 2;
#else
    mouseGetPosition(x, y);
#endif
}

} // namespace fallout
