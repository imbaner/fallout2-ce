#ifndef FALLOUT_TACTICAL_VIEW_H_
#define FALLOUT_TACTICAL_VIEW_H_

#include <vector>

#include "obj_types.h"

namespace fallout {

// CE: Combat's tactical view (touch controls). A HUD button in combat turns
// it on: on the player's turns critters and items are drawn see-through
// (`objectSetSeeThrough`), the map hints draw the tiles - a grid, everyone's
// tile in the game's combat outline colors, where the dude can walk with
// the action points left, tiles nobody can stand on - and taps and long
// presses pick tiles: a critter by the tile it stands on, items and corpses
// not at all. Hidden on other turns, off when the combat ends.

// The button.
void tacticalViewToggle();
bool tacticalViewIsOn();

// On and the player's turn: the view shows and picks tiles.
bool tacticalViewIsShown();

// Game loop: off when the combat is over, see-through drawing while shown.
void tacticalViewUpdate();

// Living critter standing on [tile] (shown on the map), nullptr - none.
Object* tacticalViewCritterAt(int tile);

// Where the dude can walk now: tiles reachable with the action points left
// (and the combat's free move), the dude's tile not included; tiles within
// that many steps something stands on that nobody can walk through
// (critters' tiles aren't - they have their own colors).
struct TacticalViewReach {
    std::vector<int> reachable;
    std::vector<int> blocked;
};
const TacticalViewReach& tacticalViewGetReach();

} // namespace fallout

#endif /* FALLOUT_TACTICAL_VIEW_H_ */
