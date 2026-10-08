#ifndef FALLOUT_SAVE_COMPATIBILITY_H_
#define FALLOUT_SAVE_COMPATIBILITY_H_

#include <string>
#include <vector>

#include "save_composition.h"

namespace fallout {

// Save compatibility (port enhancement, `[enhancements]
// save_compatibility`): what the game was made of when a save was made,
// compared with what it is made of now, marked on the save screen.
//
// The game's composition is its archives as the engine opened them
// (master.dat, critter.dat, sfall.dat, the mods' .dat...): each by a
// fingerprint of its index (names and sizes of its files) and whether it is
// Fallout 2's own (master.dat, critter.dat, patch000.dat), holds game logic
// (protos, scripts, maps, data) or only looks and sounds (art, sound,
// texts). Loose files in data\ and the port's own ce.dat aren't part of
// it.
//
// A save's composition is kept in the port's records of saves
// (save_records.h), not in the save. The first time there are records all
// saves get the game's composition; the app's import gives saves "*" - the
// game's at the next start.

// What the game is made of now (encoded, save_composition.h).
const std::string& saveCompatibilityCurrent();

// After the save of [slot] was made: the game's composition now is kept
// for it.
void saveCompatibilityRecord(int slot);

// The save of [slot] against the game now; [changes] (may be null): what
// differs that matters. kSame with the enhancement off.
SaveCompatibility saveCompatibilityCheck(int slot, std::vector<SaveCompatibilityChange>* changes);

} // namespace fallout

#endif /* FALLOUT_SAVE_COMPATIBILITY_H_ */
