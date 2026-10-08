#ifndef FALLOUT_PERF_MONITOR_H_
#define FALLOUT_PERF_MONITOR_H_

#include <SDL.h>

#include <string>

namespace fallout {

// CE: Frame timing log for finding slow screens on a device. With
// `[debug] perf_log=1` in fallout2.cfg, once a second a line goes to perf.log
// (next to the game data): the game modes on, frames per second, the longest
// frame and where a frame's time went on average - the game's own loop,
// uploading the screen and drawing the map, building and drawing the mobile
// UI, presenting, sleeping in the frame limiter.

struct PerfFrame {
    Uint64 start = 0;
    Uint64 uiStart = 0;
    Uint64 uiEnd = 0;
    Uint64 end = 0;
};

void perfMonitorMark(Uint64* time);

// Parts of a frame, timed from [start] (`perfMonitorMark`) to now.
enum class PerfSpan {
    ScreenUpload,
    WorldUpload,
    WorldDraw,
    ScreenDraw,
    Count,
};

void perfMonitorSpan(PerfSpan span, Uint64 start);

// Amounts per frame.
enum class PerfCount {
    WorldUploads,
    WorldPixels,
    ScreenPixels,
    // Pixels of the map the tile engine drew (CPU).
    WorldDrawnPixels,
    Count,
};

void perfMonitorCount(PerfCount counter, long long value);

// Amount since the game started (also without perf.log; automated tests).
long long perfMonitorGetTotal(PerfCount counter);

// The frame limiter sleeps [ms].
void perfMonitorSleep(unsigned int ms);

// A frame was presented.
void perfMonitorFrame(const PerfFrame& frame);

// Names of game modes [modes] ("dialog+barter", "map" for none).
std::string perfMonitorGameModeNames(int modes);

} // namespace fallout

#endif /* FALLOUT_PERF_MONITOR_H_ */
