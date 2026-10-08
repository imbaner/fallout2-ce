#ifndef FALLOUT_TOUCH_LOG_H_
#define FALLOUT_TOUCH_LOG_H_

#include <SDL.h>

#include "touch.h"

namespace fallout {

// CE: Touch flight recorder for telling what went wrong with touches on a
// device (`[debug] touch_log`, on by default in the touch-only build). The
// latest finger events and what the game made of them are kept in memory
// (a fixed ring, nothing else happens per event); touch.log (next to the
// game data) gets them only when something unusual happens:
// - a burst of very short taps (a finger down and up within 40 ms, several
//   in a row - no one taps like that; a system edge gesture re-sending a
//   touch looks like that too);
// - touches handled late (the game was busy while they waited);
// - finger ends lost (the game let go of a finger the system no longer has
//   down);
// - three or more fingers down at once;
// - the game going to the background or closing (the player's way to mark
//   a glitch: swipe home or close the game right after it).
// Each dump has the events since the previous one (up to 30 s): times
// relative to the dump, positions in screen pixels, the event's own time
// against when the game read it.

// Who took a finger event.
enum class TouchLogRoute {
    // The mobile UI (a screen or the HUD).
    Ui,
    // The map's gesture recognizer (touch.cc).
    Map,
    // Thrown away with the input queue (`inputEventQueueReset`).
    Drained,
};

// What touch controls did with a gesture of the map.
enum class TouchLogGestureFate {
    Handled,
    // A tap or a long press too old to be done (`kTouchStaleMs`).
    Stale,
    // Fingers with no meaning on the map.
    Ignored,
};

void touchLogFinger(const SDL_TouchFingerEvent& event, Uint32 type, TouchLogRoute route);
void touchLogGesture(const Gesture& gesture, TouchLogGestureFate fate);
// The mobile UI dropped queued events of touches that ended long ago.
void touchLogUiStaleDrop(int events);
// The map forgot a finger whose end went to the mobile UI.
void touchLogCancel(SDL_FingerID fingerId);
// Fingers were let go because their ends never came ([side]: "ui", "map").
void touchLogLostFingers(const char* side);
void touchLogBack();
void touchLogFocus(bool gained);
// The game closes: what wasn't written yet is.
void touchLogExit();

// Once per event loop: notices stalls, writes a dump waiting for its time.
void touchLogFrame();

// Automated tests (the recorder is off in them): turns it on and starts an
// empty touch.log.
void touchLogStartForTest();

} // namespace fallout

#endif /* FALLOUT_TOUCH_LOG_H_ */
