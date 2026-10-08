#ifndef FALLOUT_ACTION_LOG_H_
#define FALLOUT_ACTION_LOG_H_

#include <SDL.h>

namespace fallout {

typedef struct Object Object;

// CE: Game journal (`[debug] action_log`, always on in the touch-only
// build): what the player did and what the game made of it, from the game's
// start to its close, in actions.log next to the game data (the previous run
// in actions.log.old). For finding how a state that shouldn't be came about
// (2026-10-01: the dude kept his armor's look after the armor went to a
// party member, and no one knew how).
//
// One line per event, wall clock first:
// - the player's commands (HUD, screens' tabs), taps on the map and what
//   they did, actions on the inventory, loot and barter screens;
// - the game's screens and modes (inventory, dialog, combat...), maps;
// - saves and loads (with the state after a load);
// - changes of what the dude and his party wear and hold, noticed once per
//   frame wherever they come from (scripts too); a look that isn't what the
//   dude wears is marked "MISMATCH";
// - every change of the dude's look art, with who made it (callers);
// - the inventory's equipment globals set outside its screens ("STALE");
// - fingers down and up, the camera (center tile, zoom) - enough to replay
//   a session from a save.
// Lines are written in batches (once a second, when the game goes to the
// background or closes).

void actionLog(const char* format, ...);
// " pid 74 (Leather Jacket)", " dude", "" for nullptr.
const char* actionLogObject(Object* object);
// The dude's and the party's equipment and looks, with [title].
void actionLogState(const char* title);

// The dude's look art changes (`objectSetFrmId`): logged with the callers
// (offsets in the game's library: symbolize with the unstripped build).
void actionLogDudeFid(int oldFid, int newFid);
// A finger down or up (moves aren't), [route] - who took it.
void actionLogFinger(const SDL_TouchFingerEvent& event, Uint32 type, const char* route);

// Once per event loop: notices changes (see above), writes in batches.
void actionLogFrame();
// The game goes to the background or closes: what is waiting is written.
void actionLogFlush();

} // namespace fallout

#endif /* FALLOUT_ACTION_LOG_H_ */
