#include "perf_monitor.h"

#include <stdio.h>

#include <string>

#include "game.h"
#include "settings.h"

namespace fallout {

namespace {

    struct Totals {
        int frames = 0;
        double logic = 0.0;
        double upload = 0.0;
        double ui = 0.0;
        double present = 0.0;
        double sleep = 0.0;
        double longest = 0.0;
        double spans[static_cast<int>(PerfSpan::Count)] = {};
        long long counts[static_cast<int>(PerfCount::Count)] = {};
    };

    Uint64 gLastEnd = 0;
    Uint64 gPeriodStart = 0;
    double gSleptSinceFrame = 0.0;
    Totals gTotals;
    long long gAllCounts[static_cast<int>(PerfCount::Count)] = {};
    FILE* gLog = nullptr;

    double toMs(Uint64 from, Uint64 to)
    {
        return static_cast<double>(to - from) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    }

} // namespace

std::string perfMonitorGameModeNames(int modes)
{
    static const struct {
        int mode;
        const char* name;
    } kModes[] = {
        { GameMode::kWorldmap, "worldmap" },
        { GameMode::kDialog, "dialog" },
        { GameMode::kOptions, "options" },
        { GameMode::kCombat, "combat" },
        { GameMode::kEditor, "editor" },
        { GameMode::kPipboy, "pipboy" },
        { GameMode::kPlayerTurn, "playerturn" },
        { GameMode::kInventory, "inventory" },
        { GameMode::kAutomap, "automap" },
        { GameMode::kUseOn, "useon" },
        { GameMode::kLoot, "loot" },
        { GameMode::kBarter, "barter" },
    };

    std::string names;
    for (const auto& entry : kModes) {
        if ((modes & entry.mode) != 0) {
            if (!names.empty()) {
                names += '+';
            }
            names += entry.name;
        }
    }
    return names.empty() ? "map" : names;
}

void perfMonitorMark(Uint64* time)
{
    *time = settings.debug.perf_log ? SDL_GetPerformanceCounter() : 0;
}

void perfMonitorSpan(PerfSpan span, Uint64 start)
{
    if (settings.debug.perf_log && start != 0) {
        gTotals.spans[static_cast<int>(span)] += toMs(start, SDL_GetPerformanceCounter());
    }
}

void perfMonitorCount(PerfCount counter, long long value)
{
    gAllCounts[static_cast<int>(counter)] += value;

    if (settings.debug.perf_log) {
        gTotals.counts[static_cast<int>(counter)] += value;
    }
}

long long perfMonitorGetTotal(PerfCount counter)
{
    return gAllCounts[static_cast<int>(counter)];
}

void perfMonitorSleep(unsigned int ms)
{
    if (settings.debug.perf_log) {
        gSleptSinceFrame += ms;
    }
}

void perfMonitorFrame(const PerfFrame& frame)
{
    if (!settings.debug.perf_log) {
        return;
    }

    if (gLastEnd != 0) {
        double interval = toMs(gLastEnd, frame.end);
        gTotals.frames++;
        gTotals.logic += toMs(gLastEnd, frame.start) - gSleptSinceFrame;
        gTotals.sleep += gSleptSinceFrame;
        gTotals.upload += toMs(frame.start, frame.uiStart);
        gTotals.ui += toMs(frame.uiStart, frame.uiEnd);
        gTotals.present += toMs(frame.uiEnd, frame.end);
        if (interval > gTotals.longest) {
            gTotals.longest = interval;
        }
    } else {
        gPeriodStart = frame.end;
    }

    gLastEnd = frame.end;
    gSleptSinceFrame = 0.0;

    double elapsed = toMs(gPeriodStart, frame.end);
    if (elapsed < 1000.0 || gTotals.frames == 0) {
        return;
    }

    if (gLog == nullptr) {
        gLog = fopen("perf.log", "a");
        if (gLog == nullptr) {
            return;
        }
        fprintf(gLog, "# screen fps longest_ms | per frame ms: logic upload+map ui present sleep | screen_up world_up world_draw screen_draw ms | world uploads, world kpx, screen kpx\n");
    }

    double n = gTotals.frames;
    fprintf(gLog, "%-18s %5.1f %6.1f | %5.2f %5.2f %5.2f %5.2f %5.2f | %5.2f %5.2f %5.2f %5.2f | %4.1f %6.1f %6.1f\n",
        perfMonitorGameModeNames(GameMode::getCurrentGameMode()).c_str(),
        n * 1000.0 / elapsed,
        gTotals.longest,
        gTotals.logic / n,
        gTotals.upload / n,
        gTotals.ui / n,
        gTotals.present / n,
        gTotals.sleep / n,
        gTotals.spans[static_cast<int>(PerfSpan::ScreenUpload)] / n,
        gTotals.spans[static_cast<int>(PerfSpan::WorldUpload)] / n,
        gTotals.spans[static_cast<int>(PerfSpan::WorldDraw)] / n,
        gTotals.spans[static_cast<int>(PerfSpan::ScreenDraw)] / n,
        gTotals.counts[static_cast<int>(PerfCount::WorldUploads)] / n,
        gTotals.counts[static_cast<int>(PerfCount::WorldPixels)] / n / 1000.0,
        gTotals.counts[static_cast<int>(PerfCount::ScreenPixels)] / n / 1000.0);
    fflush(gLog);

    gTotals = Totals();
    gPeriodStart = frame.end;
}

} // namespace fallout
