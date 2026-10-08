#ifndef FALLOUT_DEV_AUTOTEST_H_
#define FALLOUT_DEV_AUTOTEST_H_

#include <SDL.h>

namespace fallout {

// CE: Scripted developer checks of the world view (zoom and pan), enabled
// with `--dev-autotest=<output directory>`. Runs a fixed sequence of view
// changes once the map is loaded, saves every rendered result as PNG into the
// output directory and quits the game.

void devAutotestInit(const char* outputPath);

// Selects script: "worldmap" checks world map touch controls, default script
// checks the map, inventory and combat.
void devAutotestSetScenario(const char* name);
bool devAutotestIsEnabled();

// Advances the script, called once per main loop iteration.
void devAutotestTick();

// Advances synthetic touch, called on every presented frame (works inside any
// modal loop).
void devAutotestPump();

// Saves the frame being presented if the script asked for it.
void devAutotestCapture(SDL_Renderer* renderer);

// Capture a specific transition frame from inside a synchronous UI action.
void devAutotestCaptureNextFrame(const char* name);

// A line in the script's log (nothing without a script).
void devAutotestNote(const char* format, ...);

} // namespace fallout

#endif /* FALLOUT_DEV_AUTOTEST_H_ */
