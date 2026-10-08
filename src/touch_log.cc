#include "touch_log.h"

#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <string>

#include "dev_autotest.h"
#include "game.h"
#include "hud_layout.h"
#include "perf_monitor.h"
#include "settings.h"
#include "svga.h"

namespace fallout {

namespace {

    constexpr const char* kFileName = "touch.log";
    constexpr const char* kOldFileName = "touch.log.old";
    constexpr long kMaxFileSize = 2 * 1024 * 1024;

    // A tap this short (ms) and still (part of the screen) is no one's tap.
    constexpr Uint32 kShortTapMs = 40;
    constexpr float kShortTapDistance = 0.015f;
    // So many short taps within the time make a burst.
    constexpr int kShortTapBurst = 4;
    constexpr Uint32 kShortTapBurstMs = 3000;

    // Recorded after a trigger before the dump.
    constexpr Uint32 kAfterTriggerMs = 3000;
    // Automatic dumps at most this often.
    constexpr Uint32 kMinDumpIntervalMs = 10000;
    // A dump goes back at most this far.
    constexpr Uint32 kDumpSpanMs = 30000;
    // The event loop not run for this long is a stall.
    constexpr Uint32 kStallMs = 300;
    // A finger's moves recorded at most this often, a pan's changes too.
    constexpr Uint32 kMoveIntervalMs = 25;
    constexpr Uint32 kPanChangeIntervalMs = 100;

    enum class Kind : unsigned char {
        FingerDown,
        FingerMove,
        FingerUp,
        Gesture,
        UiStale,
        Cancel,
        LostFingers,
        Back,
        FocusLost,
        FocusGained,
        Stall,
        Mode,
        Trigger,
    };

    struct Record {
        // When the game read it (SDL ticks).
        Uint32 time;
        // Fingers: the event's own time; gestures: when they happened.
        Uint32 eventTime;
        Kind kind;
        // Fingers: `TouchLogRoute`; gestures: `TouchLogGestureFate`.
        unsigned char route;
        // Gestures: type and state.
        unsigned char type;
        unsigned char state;
        // Fingers: id; gestures: fingers; counts, stall ms, game modes.
        long long value;
        // Finger up: ms it was down (-1 unknown).
        int held;
        // Fingers: 0..1 of the screen; gestures: game pixels.
        float x;
        float y;
        float pressure;
        // Trigger reasons, sides of lost fingers (string literals).
        const char* text;
    };

    constexpr int kRecords = 2048;
    Record gRecords[kRecords];
    // Records ever made; the ones before `gDumped` are in touch.log.
    unsigned long long gRecorded = 0;
    unsigned long long gDumped = 0;

    bool gForced = false;

    // Fingers down (for tap lengths).
    struct Finger {
        bool used;
        SDL_FingerID id;
        Uint32 downTime;
        Uint32 lastMoveTime;
        float x;
        float y;
    };

    constexpr int kFingers = 10;
    Finger gFingers[kFingers];

    Uint32 gShortTaps[kShortTapBurst];
    int gShortTapCount = 0;

    int gLastMode = -1;
    Uint32 gLastFrameTime = 0;
    Uint32 gLastPanChangeTime = 0;
    bool gLateNoted = false;

    bool gDumpPending = false;
    Uint32 gDumpTime = 0;
    std::string gDumpReason;
    bool gAutoDumped = false;
    Uint32 gLastAutoDumpTime = 0;

    bool enabled()
    {
        return gForced || (settings.debug.touch_log && !devAutotestIsEnabled());
    }

    Record* add(Kind kind, Uint32 now)
    {
        int mode = GameMode::getCurrentGameMode();
        if (mode != gLastMode && kind != Kind::Mode) {
            gLastMode = mode;
            Record* record = add(Kind::Mode, now);
            record->value = mode;
        }

        Record* record = &(gRecords[gRecorded % kRecords]);
        gRecorded++;
        *record = Record();
        record->time = now;
        record->eventTime = now;
        record->kind = kind;
        record->held = -1;
        return record;
    }

    void trigger(const char* reason, Uint32 now)
    {
        Record* record = add(Kind::Trigger, now);
        record->text = reason;

        if (gDumpPending) {
            if (gDumpReason.find(reason) == std::string::npos) {
                gDumpReason += ", ";
                gDumpReason += reason;
            }
            return;
        }

        // Shows up in the next dump.
        if (gAutoDumped && now - gLastAutoDumpTime < kMinDumpIntervalMs) {
            return;
        }

        gDumpPending = true;
        gDumpTime = now + kAfterTriggerMs;
        gDumpReason = reason;
    }

    Finger* findFinger(SDL_FingerID id)
    {
        for (Finger& finger : gFingers) {
            if (finger.used && finger.id == id) {
                return &finger;
            }
        }
        return nullptr;
    }

    Finger* addFinger(SDL_FingerID id)
    {
        Finger* finger = findFinger(id);
        if (finger == nullptr) {
            // Full: fingers whose ends never came, the oldest goes.
            finger = &(gFingers[0]);
            for (Finger& candidate : gFingers) {
                if (!candidate.used) {
                    finger = &candidate;
                    break;
                }
                if (candidate.downTime < finger->downTime) {
                    finger = &candidate;
                }
            }
        }
        finger->used = true;
        finger->id = id;
        return finger;
    }

    int fingersDown()
    {
        int count = 0;
        int devices = SDL_GetNumTouchDevices();
        for (int device = 0; device < devices; device++) {
            count += SDL_GetNumTouchFingers(SDL_GetTouchDevice(device));
        }
        return count;
    }

    void noteShortTap(Uint32 now)
    {
        if (gShortTapCount == kShortTapBurst) {
            std::copy(gShortTaps + 1, gShortTaps + kShortTapBurst, gShortTaps);
            gShortTapCount--;
        }
        gShortTaps[gShortTapCount++] = now;

        if (gShortTapCount == kShortTapBurst && now - gShortTaps[0] <= kShortTapBurstMs) {
            gShortTapCount = 0;
            trigger("burst of very short taps", now);
        }
    }

    const char* gestureName(int type)
    {
        switch (type) {
        case kTap:
            return "tap";
        case kLongPress:
            return "long press";
        case kPan:
            return "pan";
        default:
            return "unrecognized";
        }
    }

    const char* gestureStateName(int state)
    {
        switch (state) {
        case kBegan:
            return " began";
        case kChanged:
            return " changed";
        case kEnded:
            return " ended";
        default:
            return "";
        }
    }

    const char* routeName(int route)
    {
        switch (static_cast<TouchLogRoute>(route)) {
        case TouchLogRoute::Ui:
            return "ui";
        case TouchLogRoute::Map:
            return "map";
        default:
            return "drained";
        }
    }

    const char* fateName(int fate)
    {
        switch (static_cast<TouchLogGestureFate>(fate)) {
        case TouchLogGestureFate::Handled:
            return "done";
        case TouchLogGestureFate::Stale:
            return "dropped: stale";
        default:
            return "ignored";
        }
    }

    FILE* openFile()
    {
        FILE* stream = fopen(kFileName, "a");
        if (stream == nullptr) {
            return nullptr;
        }

        fseek(stream, 0, SEEK_END);
        long size = ftell(stream);
        if (size > kMaxFileSize) {
            fclose(stream);
            remove(kOldFileName);
            rename(kFileName, kOldFileName);
            stream = fopen(kFileName, "a");
            if (stream == nullptr) {
                return nullptr;
            }
            size = 0;
        }

        if (size == 0) {
            fprintf(stream, "# Touch recorder (see touch_log.h). Times: seconds before the dump.\n"
                            "# Fingers: screen pixels, late = ms from the touch to the game reading it,\n"
                            "# held = ms the finger was down. Gestures of the map: game pixels.\n");
        }
        return stream;
    }

    void dump(const char* reason, Uint32 now)
    {
        gDumpPending = false;

        unsigned long long first = std::max(gDumped, gRecorded > kRecords ? gRecorded - kRecords : 0ULL);
        while (first < gRecorded && now - gRecords[first % kRecords].time > kDumpSpanMs) {
            first++;
        }
        gDumped = gRecorded;
        if (first >= gRecorded) {
            return;
        }

        FILE* stream = openFile();
        if (stream == nullptr) {
            return;
        }

        int outputWidth = 0;
        int outputHeight = 0;
        if (gSdlRenderer != nullptr) {
            SDL_GetRendererOutputSize(gSdlRenderer, &outputWidth, &outputHeight);
        }
        HudMetrics metrics = hudMetricsGet();

        char date[32];
        time_t wallTime = time(nullptr);
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&wallTime));

        fprintf(stream, "\n=== %s  %s\n", date, reason);
        fprintf(stream, "    screen %dx%d, game %dx%d, cutout insets (game px) left %d top %d right %d bottom %d, %s, fingers down %d\n",
            outputWidth,
            outputHeight,
            metrics.screenWidth,
            metrics.screenHeight,
            metrics.insetLeft,
            metrics.insetTop,
            metrics.insetRight,
            metrics.insetBottom,
            perfMonitorGameModeNames(GameMode::getCurrentGameMode()).c_str(),
            fingersDown());

        for (unsigned long long index = first; index < gRecorded; index++) {
            const Record& record = gRecords[index % kRecords];
            Uint32 ago = now - record.time;
            fprintf(stream, "%3u.%03u  ", ago / 1000, ago % 1000);

            switch (record.kind) {
            case Kind::FingerDown:
            case Kind::FingerMove:
            case Kind::FingerUp:
                fprintf(stream, "%-4s f%-3lld x %4.0f y %4.0f  p %.2f  late %3u",
                    record.kind == Kind::FingerDown ? "down" : (record.kind == Kind::FingerMove ? "move" : "up"),
                    record.value,
                    record.x * outputWidth,
                    record.y * outputHeight,
                    record.pressure,
                    record.time - record.eventTime);
                if (record.held >= 0) {
                    fprintf(stream, "  held %d", record.held);
                }
                fprintf(stream, "  -> %s\n", routeName(record.route));
                break;
            case Kind::Gesture:
                fprintf(stream, "map %s%s, %lld finger(s) at %.0f,%.0f, %u ms old: %s\n",
                    gestureName(record.type),
                    gestureStateName(record.state),
                    record.value,
                    record.x,
                    record.y,
                    record.time - record.eventTime,
                    fateName(record.route));
                break;
            case Kind::UiStale:
                fprintf(stream, "ui dropped %lld queued events of touches that ended long ago\n", record.value);
                break;
            case Kind::Cancel:
                fprintf(stream, "map let go of f%lld (its end went to the ui)\n", record.value);
                break;
            case Kind::LostFingers:
                fprintf(stream, "%s let go of fingers whose ends never came\n", record.text);
                break;
            case Kind::Back:
                fprintf(stream, "back (system gesture or button)\n");
                break;
            case Kind::FocusLost:
                fprintf(stream, "game lost focus\n");
                break;
            case Kind::FocusGained:
                fprintf(stream, "game got focus\n");
                break;
            case Kind::Stall:
                fprintf(stream, "game busy for %lld ms (no input read)\n", record.value);
                break;
            case Kind::Mode:
                fprintf(stream, "mode: %s\n", perfMonitorGameModeNames(static_cast<int>(record.value)).c_str());
                break;
            case Kind::Trigger:
                fprintf(stream, "! %s\n", record.text);
                break;
            }
        }

        fclose(stream);
    }

} // namespace

void touchLogFinger(const SDL_TouchFingerEvent& event, Uint32 type, TouchLogRoute route)
{
    if (!enabled()) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    Finger* finger = findFinger(event.fingerId);

    if (type == SDL_FINGERMOTION) {
        if (finger != nullptr) {
            if (event.timestamp - finger->lastMoveTime < kMoveIntervalMs) {
                return;
            }
            finger->lastMoveTime = event.timestamp;
        }
    }

    Kind kind = type == SDL_FINGERDOWN ? Kind::FingerDown : (type == SDL_FINGERMOTION ? Kind::FingerMove : Kind::FingerUp);
    Record* record = add(kind, now);
    record->eventTime = event.timestamp;
    record->route = static_cast<unsigned char>(route);
    record->value = static_cast<long long>(event.fingerId);
    record->x = event.x;
    record->y = event.y;
    record->pressure = event.pressure;

    if (type == SDL_FINGERDOWN) {
        finger = addFinger(event.fingerId);
        finger->downTime = event.timestamp;
        finger->lastMoveTime = event.timestamp;
        finger->x = event.x;
        finger->y = event.y;

        if (fingersDown() >= 3) {
            trigger("three or more fingers down", now);
        }
    } else if (type == SDL_FINGERUP && finger != nullptr) {
        record->held = static_cast<int>(event.timestamp - finger->downTime);
        float dx = event.x - finger->x;
        float dy = event.y - finger->y;
        if (static_cast<Uint32>(record->held) < kShortTapMs && dx * dx + dy * dy < kShortTapDistance * kShortTapDistance) {
            noteShortTap(now);
        }
        finger->used = false;
    }

    // Touches waited while the game was busy: noted once per stall.
    if (route != TouchLogRoute::Drained) {
        if (now - event.timestamp > kTouchStaleMs) {
            if (!gLateNoted) {
                gLateNoted = true;
                trigger("touches read late", now);
            }
        } else {
            gLateNoted = false;
        }
    }
}

void touchLogGesture(const Gesture& gesture, TouchLogGestureFate fate)
{
    if (!enabled()) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    if (gesture.type == kPan && gesture.state == kChanged && fate == TouchLogGestureFate::Handled) {
        if (now - gLastPanChangeTime < kPanChangeIntervalMs) {
            return;
        }
        gLastPanChangeTime = now;
    }

    Record* record = add(Kind::Gesture, now);
    record->eventTime = gesture.time;
    record->route = static_cast<unsigned char>(fate);
    record->type = static_cast<unsigned char>(gesture.type);
    record->state = static_cast<unsigned char>(gesture.state);
    record->value = gesture.numberOfTouches;
    record->x = static_cast<float>(gesture.x);
    record->y = static_cast<float>(gesture.y);
}

void touchLogUiStaleDrop(int events)
{
    if (!enabled()) {
        return;
    }

    add(Kind::UiStale, SDL_GetTicks())->value = events;
}

void touchLogCancel(SDL_FingerID fingerId)
{
    if (!enabled()) {
        return;
    }

    add(Kind::Cancel, SDL_GetTicks())->value = static_cast<long long>(fingerId);
}

void touchLogLostFingers(const char* side)
{
    if (!enabled()) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    add(Kind::LostFingers, now)->text = side;
    trigger("finger ends lost", now);
}

void touchLogBack()
{
    if (!enabled()) {
        return;
    }

    add(Kind::Back, SDL_GetTicks());
}

void touchLogFocus(bool gained)
{
    if (!enabled()) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    add(gained ? Kind::FocusGained : Kind::FocusLost, now);

    if (!gained) {
        for (Finger& finger : gFingers) {
            finger.used = false;
        }

        // The game may not come back (the system closes it).
        std::string reason = "left the game";
        if (gDumpPending) {
            reason += " (after: " + gDumpReason + ")";
        }
        dump(reason.c_str(), now);
    }
}

void touchLogExit()
{
    if (!enabled()) {
        return;
    }

    std::string reason = "game closed";
    if (gDumpPending) {
        reason += " (after: " + gDumpReason + ")";
    }
    dump(reason.c_str(), SDL_GetTicks());
}

void touchLogFrame()
{
    if (!enabled()) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    if (gLastFrameTime != 0 && now - gLastFrameTime > kStallMs) {
        add(Kind::Stall, now)->value = now - gLastFrameTime;
    }
    gLastFrameTime = now;

    if (gDumpPending && static_cast<int>(now - gDumpTime) >= 0) {
        std::string reason = gDumpReason;
        dump(reason.c_str(), now);
        gAutoDumped = true;
        gLastAutoDumpTime = now;
    }
}

void touchLogStartForTest()
{
    gForced = true;
    remove(kFileName);
    gDumped = gRecorded;
    gDumpPending = false;
    gAutoDumped = false;
    gShortTapCount = 0;
    gLateNoted = false;
}

} // namespace fallout
