#ifndef FALLOUT_TOUCH_H_
#define FALLOUT_TOUCH_H_

#include <SDL.h>

// CE: Touch-only build (Android; `FALLOUT_FORCE_TOUCH_ONLY` makes a desktop
// one for automated tests): fingers are the only pointer. The mobile UI,
// touch HUD, touch controls and the world view are always on (settings.cc;
// `muiIsEnabled` and `touchHudIsEnabled` are constants), the game's windows
// the mobile UI replaced aren't built, there's no mouse at all:
// nothing is emulated from touches (the emulation isn't built either), a
// connected mouse is ignored, no system cursor.
#if defined(__ANDROID__) || defined(FALLOUT_FORCE_TOUCH_ONLY)
#define FALLOUT_TOUCH_ONLY 1
#else
#define FALLOUT_TOUCH_ONLY 0
#endif

namespace fallout {

enum GestureType {
    kUnrecognized,
    kTap,
    kLongPress,
    kPan,
};

enum GestureState {
    kPossible,
    kBegan,
    kChanged,
    kEnded,
};

struct Gesture {
    GestureType type;
    GestureState state;
    int numberOfTouches;
    int x;
    int y;
    // CE: Distance between the first two fingers (0 for single finger
    // gestures), used for pinch zoom.
    int span;
    // CE: Centroid and span when fingers touched the screen (gesture is
    // recognized only after fingers move a bit).
    int startX;
    int startY;
    int startSpan;
    // CE: When the gesture happened (SDL ticks): a tap's fingers lifted, a
    // long press recognized (its finger still down), a pan's latest finger
    // event. A gesture handled much later (the game stalled) is stale.
    Uint32 time;
};

// CE: A tap or a long press this old (ms) when it would be handled isn't
// done any more (the mobile UI drops such touches too).
constexpr Uint32 kTouchStaleMs = 400;

void touch_handle_start(SDL_TouchFingerEvent* event);
void touch_handle_move(SDL_TouchFingerEvent* event);
void touch_handle_end(SDL_TouchFingerEvent* event);
// CE: Forgets finger [fingerId] without a gesture: its end went to the
// mobile UI (a screen opened while it was down).
// Returns true if it was followed.
bool touch_handle_cancel(SDL_FingerID fingerId);
// CE: Forgets fingers not in [down] (the [count] fingers really down now):
// ends that never came.
// Returns true if any was forgotten.
bool touch_forget_missing(const SDL_FingerID* down, int count);
void touch_process_gesture();
// CE: A finger followed by the recognizer is on the screen.
bool touch_any_finger_down();
bool touch_get_gesture(Gesture* gesture);
// How long a finger is held for a long press, ms: Android's touch & hold
// delay (Accessibility), 500 elsewhere.
unsigned int touchLongPressMs();
// Reads it again (the game came back: it may have been changed meanwhile).
void touchRefreshLongPressMs();

void touch_set_touchscreen_mode(const bool value);
bool touch_get_touchscreen_mode();
void touch_set_pan_mode(const bool value);
bool touch_get_pan_mode();

} // namespace fallout

#endif /* FALLOUT_TOUCH_H_ */
