#include "fps_limiter.h"

#include <SDL.h>

#include "perf_monitor.h"

namespace fallout {

FpsLimiter::FpsLimiter(unsigned int fps)
    : _fps(fps)
    , _ticks(0)
{
}

void FpsLimiter::mark()
{
    _ticks = SDL_GetTicks();
}

void FpsLimiter::throttle() const
{
    if (1000 / _fps > SDL_GetTicks() - _ticks) {
        unsigned int delay = 1000 / _fps - (SDL_GetTicks() - _ticks);
        perfMonitorSleep(delay);
        SDL_Delay(delay);
    }
}

} // namespace fallout
