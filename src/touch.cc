#include "touch.h"

#include <math.h>

#include <algorithm>
#include <queue>

#if !FALLOUT_TOUCH_ONLY
#include "mouse.h"
#endif
#include "svga.h"
#include "hud_layout.h"
#include "touch_controls.h"

namespace fallout {

#define TOUCH_PHASE_BEGAN 0
#define TOUCH_PHASE_MOVED 1
#define TOUCH_PHASE_ENDED 2

#define MAX_TOUCHES 10

#define TAP_MAXIMUM_DURATION 75
#define PAN_MINIMUM_MOVEMENT 4

// CE: A finger moving less than this is still a tap / long press (Android's
// touch slop). The original 4 screen pixels were made for 640x480 screens;
// on a phone they're under a millimeter and a finger rolling while tapping
// made a pan, the tap was lost.
static constexpr float kTouchSlopDp = 8.0f;

static int touch_get_slop()
{
    return std::max(PAN_MINIMUM_MOVEMENT, static_cast<int>(kTouchSlopDp * hudGetPixelsPerDp()));
}
#define LONG_PRESS_MINIMUM_DURATION 500

struct TouchLocation {
    int x;
    int y;
};

struct Touch {
    bool used;
    SDL_FingerID fingerId;
    TouchLocation startLocation;
    Uint32 startTimestamp;
    TouchLocation currentLocation;
    Uint32 currentTimestamp;
    int phase;
};

static Touch touches[MAX_TOUCHES];
static Gesture currentGesture;
// CE: FIFO, several events may be queued at once (e.g. when two finger pan
// replaces single finger one).
static std::queue<Gesture> gestureEventsQueue;

static bool gUseTouchscreenMode = false;
static bool gUsePanMode = false;

static int find_touch(SDL_FingerID fingerId)
{
    for (int index = 0; index < MAX_TOUCHES; index++) {
        if (touches[index].fingerId == fingerId) {
            return index;
        }
    }
    return -1;
}

static int find_unused_touch_index()
{
    for (int index = 0; index < MAX_TOUCHES; index++) {
        if (!touches[index].used) {
            return index;
        }
    }
    return -1;
}

static TouchLocation touch_get_start_location_centroid(int* indexes, int length)
{
    TouchLocation centroid;
    centroid.x = 0;
    centroid.y = 0;
    for (int index = 0; index < length; index++) {
        centroid.x += touches[indexes[index]].startLocation.x;
        centroid.y += touches[indexes[index]].startLocation.y;
    }
    centroid.x /= length;
    centroid.y /= length;
    return centroid;
}

static TouchLocation touch_get_current_location_centroid(int* indexes, int length)
{
    TouchLocation centroid;
    centroid.x = 0;
    centroid.y = 0;
    for (int index = 0; index < length; index++) {
        centroid.x += touches[indexes[index]].currentLocation.x;
        centroid.y += touches[indexes[index]].currentLocation.y;
    }
    centroid.x /= length;
    centroid.y /= length;
    return centroid;
}

// CE: Latest event time of the fingers.
static Uint32 touch_get_latest_timestamp(int* indexes, int length)
{
    Uint32 latest = 0;
    for (int index = 0; index < length; index++) {
        latest = std::max(latest, touches[indexes[index]].currentTimestamp);
    }
    return latest;
}

// CE: Distance between the first two fingers, or 0.
static int touch_get_span(int* indexes, int length, bool current)
{
    if (length < 2) {
        return 0;
    }

    const TouchLocation& a = current ? touches[indexes[0]].currentLocation : touches[indexes[0]].startLocation;
    const TouchLocation& b = current ? touches[indexes[1]].currentLocation : touches[indexes[1]].startLocation;
    return static_cast<int>(hypot(a.x - b.x, a.y - b.y));
}

void touch_handle_start(SDL_TouchFingerEvent* event)
{
    // On iOS `fingerId` is an address of underlying `UITouch` object. When
    // `touchesBegan` is called this object might be reused, but with
    // incresed `tapCount` (which is ignored in this implementation).
    int index = find_touch(event->fingerId);
    if (index == -1) {
        index = find_unused_touch_index();
    }

    if (index != -1) {
        Touch* touch = &(touches[index]);
        touch->used = true;
        touch->fingerId = event->fingerId;
        touch->startTimestamp = event->timestamp;
        touch->startLocation.x = static_cast<int>(event->x * screenGetWidth());
        touch->startLocation.y = static_cast<int>(event->y * screenGetHeight());
        touch->currentTimestamp = touch->startTimestamp;
        touch->currentLocation = touch->startLocation;
        touch->phase = TOUCH_PHASE_BEGAN;
    }
}

void touch_handle_move(SDL_TouchFingerEvent* event)
{
    int index = find_touch(event->fingerId);
    if (index != -1) {
        Touch* touch = &(touches[index]);
        touch->currentTimestamp = event->timestamp;
        touch->currentLocation.x = static_cast<int>(event->x * screenGetWidth());
        touch->currentLocation.y = static_cast<int>(event->y * screenGetHeight());
        touch->phase = TOUCH_PHASE_MOVED;
    }
}

void touch_handle_end(SDL_TouchFingerEvent* event)
{
    int index = find_touch(event->fingerId);
    if (index != -1) {
        Touch* touch = &(touches[index]);
        touch->currentTimestamp = event->timestamp;
        touch->currentLocation.x = static_cast<int>(event->x * screenGetWidth());
        touch->currentLocation.y = static_cast<int>(event->y * screenGetHeight());
        touch->phase = TOUCH_PHASE_ENDED;
    }
}

bool touch_handle_cancel(SDL_FingerID fingerId)
{
    int index = find_touch(fingerId);
    if (index == -1 || !touches[index].used) {
        return false;
    }

    touches[index].used = false;

    // Nothing left: no gesture goes on.
    for (const Touch& touch : touches) {
        if (touch.used) {
            return true;
        }
    }
    currentGesture = Gesture();
    return true;
}

bool touch_forget_missing(const SDL_FingerID* down, int count)
{
    bool forgotten = false;
    for (const Touch& touch : touches) {
        if (!touch.used) {
            continue;
        }

        bool isDown = false;
        for (int index = 0; index < count; index++) {
            if (down[index] == touch.fingerId) {
                isDown = true;
                break;
            }
        }

        if (!isDown && touch_handle_cancel(touch.fingerId)) {
            forgotten = true;
        }
    }
    return forgotten;
}

void touch_process_gesture()
{
    Uint32 sequenceStartTimestamp = -1;
    int sequenceStartIndex = -1;

    // Find start of sequence (earliest touch).
    for (int index = 0; index < MAX_TOUCHES; index++) {
        if (touches[index].used) {
            if (sequenceStartTimestamp > touches[index].startTimestamp) {
                sequenceStartTimestamp = touches[index].startTimestamp;
                sequenceStartIndex = index;
            }
        }
    }

    if (sequenceStartIndex == -1) {
        return;
    }

    Uint32 sequenceEndTimestamp = -1;
    if (touches[sequenceStartIndex].phase == TOUCH_PHASE_ENDED) {
        sequenceEndTimestamp = touches[sequenceStartIndex].currentTimestamp;

        // Find end timestamp of sequence.
        for (int index = 0; index < MAX_TOUCHES; index++) {
            if (touches[index].used
                && touches[index].startTimestamp >= sequenceStartTimestamp
                && touches[index].startTimestamp <= sequenceEndTimestamp) {
                if (touches[index].phase == TOUCH_PHASE_ENDED) {
                    if (sequenceEndTimestamp < touches[index].currentTimestamp) {
                        sequenceEndTimestamp = touches[index].currentTimestamp;

                        // Start over since we can have fingers missed.
                        index = -1;
                    }
                } else {
                    // Sequence is current.
                    sequenceEndTimestamp = -1;
                    break;
                }
            }
        }
    }

    int active[MAX_TOUCHES];
    int activeCount = 0;

    int ended[MAX_TOUCHES];
    int endedCount = 0;

    // Split participating fingers into two buckets - active fingers (currently
    // on screen) and ended (lifted up).
    for (int index = 0; index < MAX_TOUCHES; index++) {
        if (touches[index].used
            && touches[index].currentTimestamp >= sequenceStartTimestamp
            && touches[index].currentTimestamp <= sequenceEndTimestamp) {
            if (touches[index].phase == TOUCH_PHASE_ENDED) {
                ended[endedCount++] = index;
            } else {
                active[activeCount++] = index;
            }

            // If this sequence is over, unmark participating finger as used.
            if (sequenceEndTimestamp != -1) {
                touches[index].used = false;
            }
        }
    }

    if (currentGesture.type == kPan || currentGesture.type == kLongPress) {
        if (currentGesture.state != kEnded) {
            // For continuous gestures we want number of fingers to remain the
            // same as it was when gesture was recognized.
            if (activeCount == currentGesture.numberOfTouches && endedCount == 0) {
                TouchLocation centroid = touch_get_current_location_centroid(active, activeCount);
                int span = touch_get_span(active, activeCount, true);

                // CE: This is called on every event poll, report only actual
                // changes of pans to avoid flooding the queue. Long press is
                // reported continuously: mouse emulation keeps button held
                // only while it receives events.
                if (currentGesture.type == kLongPress
                    || currentGesture.state == kBegan
                    || centroid.x != currentGesture.x
                    || centroid.y != currentGesture.y
                    || span != currentGesture.span) {
                    currentGesture.state = kChanged;
                    currentGesture.x = centroid.x;
                    currentGesture.y = centroid.y;
                    currentGesture.span = span;
                    currentGesture.time = touch_get_latest_timestamp(active, activeCount);
                    gestureEventsQueue.push(currentGesture);
                }
            } else if (currentGesture.type == kPan && currentGesture.numberOfTouches == 1 && activeCount == 2 && endedCount == 0) {
                // CE: Second finger joined single finger pan - turn it into
                // two finger pan (pinch) right away.
                currentGesture.state = kEnded;
                currentGesture.time = touch_get_latest_timestamp(active, activeCount);
                gestureEventsQueue.push(currentGesture);

                TouchLocation centroid = touch_get_current_location_centroid(active, activeCount);
                currentGesture.type = kPan;
                currentGesture.state = kBegan;
                currentGesture.numberOfTouches = 2;
                currentGesture.x = centroid.x;
                currentGesture.y = centroid.y;
                currentGesture.span = touch_get_span(active, activeCount, true);
                currentGesture.startX = currentGesture.x;
                currentGesture.startY = currentGesture.y;
                currentGesture.startSpan = currentGesture.span;
                currentGesture.time = touch_get_latest_timestamp(active, activeCount);
                gestureEventsQueue.push(currentGesture);
            } else {
                currentGesture.state = kEnded;
                currentGesture.time = std::max(touch_get_latest_timestamp(active, activeCount), touch_get_latest_timestamp(ended, endedCount));
                gestureEventsQueue.push(currentGesture);
            }
        }

        // Reset continuous gesture if when current sequence is over.
        if (currentGesture.state == kEnded && sequenceEndTimestamp != -1) {
            currentGesture.type = kUnrecognized;
        }
    } else {
        if (activeCount == 0 && endedCount != 0) {
            // For taps we need all participating fingers to be both started
            // and ended simultaneously (within predefined threshold).
            Uint32 startEarliestTimestamp = -1;
            Uint32 startLatestTimestamp = 0;
            Uint32 endEarliestTimestamp = -1;
            Uint32 endLatestTimestamp = 0;

            for (int index = 0; index < endedCount; index++) {
                startEarliestTimestamp = std::min(startEarliestTimestamp, touches[ended[index]].startTimestamp);
                startLatestTimestamp = std::max(startLatestTimestamp, touches[ended[index]].startTimestamp);
                endEarliestTimestamp = std::min(endEarliestTimestamp, touches[ended[index]].currentTimestamp);
                endLatestTimestamp = std::max(endLatestTimestamp, touches[ended[index]].currentTimestamp);
            }

            if (startLatestTimestamp - startEarliestTimestamp <= TAP_MAXIMUM_DURATION
                && endLatestTimestamp - endEarliestTimestamp <= TAP_MAXIMUM_DURATION) {
                TouchLocation currentCentroid = touch_get_current_location_centroid(ended, endedCount);

                currentGesture.type = kTap;
                currentGesture.state = kEnded;
                currentGesture.numberOfTouches = endedCount;
                currentGesture.x = currentCentroid.x;
                currentGesture.y = currentCentroid.y;
                currentGesture.span = 0;
                currentGesture.startX = currentCentroid.x;
                currentGesture.startY = currentCentroid.y;
                currentGesture.startSpan = 0;
                currentGesture.time = endLatestTimestamp;
                gestureEventsQueue.push(currentGesture);

                // Reset tap gesture immediately.
                currentGesture.type = kUnrecognized;
            }
        } else if (activeCount != 0 && endedCount == 0) {
            TouchLocation startCentroid = touch_get_start_location_centroid(active, activeCount);
            TouchLocation currentCentroid = touch_get_current_location_centroid(active, activeCount);

            // CE: Pinch changes distance between fingers while their
            // centroid may stay in place.
            int startSpan = touch_get_span(active, activeCount, false);
            int currentSpan = touch_get_span(active, activeCount, true);

            // Disambiguate between pan and long press.
            int slop = touch_get_slop();
            if (abs(currentCentroid.x - startCentroid.x) >= slop
                || abs(currentCentroid.y - startCentroid.y) >= slop
                || abs(currentSpan - startSpan) >= slop) {
                currentGesture.type = kPan;
                currentGesture.state = kBegan;
                currentGesture.numberOfTouches = activeCount;
                currentGesture.x = currentCentroid.x;
                currentGesture.y = currentCentroid.y;
                currentGesture.span = currentSpan;
                currentGesture.startX = startCentroid.x;
                currentGesture.startY = startCentroid.y;
                currentGesture.startSpan = startSpan;
                currentGesture.time = touch_get_latest_timestamp(active, activeCount);
                gestureEventsQueue.push(currentGesture);
            } else if (SDL_GetTicks() - touches[active[0]].startTimestamp >= LONG_PRESS_MINIMUM_DURATION) {
                currentGesture.type = kLongPress;
                currentGesture.state = kBegan;
                currentGesture.numberOfTouches = activeCount;
                currentGesture.x = currentCentroid.x;
                currentGesture.y = currentCentroid.y;
                currentGesture.span = currentSpan;
                currentGesture.startX = startCentroid.x;
                currentGesture.startY = startCentroid.y;
                currentGesture.startSpan = startSpan;
                // The finger held still has no newer events: the long press
                // is as new as its recognition (the finger is still down).
                currentGesture.time = SDL_GetTicks();
                gestureEventsQueue.push(currentGesture);
            }

#if !FALLOUT_TOUCH_ONLY
            // CE: Multi-finger gestures (pinch zoom) must not move cursor.
            // Touch-native input has no cursor: the mouse isn't emulated.
            if (!touchControlsIsNative() && touch_get_touchscreen_mode() && activeCount == 1 && touchControlsWantsCursorWarp(currentCentroid.x, currentCentroid.y)) {
                int x = currentCentroid.x;
                int y = currentCentroid.y;
                touchControlsAdjustUiTouch(&x, &y);

                mouseHideCursor();
                _mouse_set_position(x, y);
                mouseShowCursor();
            }
#endif
        }
    }
}

bool touch_any_finger_down()
{
    for (const Touch& touch : touches) {
        if (touch.used && touch.phase != TOUCH_PHASE_ENDED) {
            return true;
        }
    }
    return false;
}

bool touch_get_gesture(Gesture* gesture)
{
    if (gestureEventsQueue.empty()) {
        return false;
    }

    *gesture = gestureEventsQueue.front();
    gestureEventsQueue.pop();

    return true;
}

void touch_set_touchscreen_mode(const bool value)
{
    gUseTouchscreenMode = value;
}

bool touch_get_touchscreen_mode()
{
    // CE: With touch controls fingers always point at things directly.
    return gUseTouchscreenMode || touchControlsIsEnabled();
}

void touch_set_pan_mode(const bool value)
{
    gUsePanMode = value;
}

bool touch_get_pan_mode()
{
    return gUsePanMode;
}
} // namespace fallout
