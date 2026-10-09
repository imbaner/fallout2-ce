#ifndef FALLOUT_TACTICAL_VIEW_H_
#define FALLOUT_TACTICAL_VIEW_H_

#include <vector>

#include "obj_types.h"

namespace fallout {

// CE: Combat's tactical view (touch controls). A HUD button in combat turns
// it on: on the player's turns critters and items are drawn see-through
// with every critter's outline (`objectSetSeeThrough`); the map hints draw
// a faint grid and the border of where the dude can walk with the action
// points left, everyone's tile in the game's combat outline colors, then
// the outlines and the dude again on top (`objectSeeThroughTopLayer`);
// taps and long presses pick tiles: a critter by the tile it stands on,
// items and corpses not at all. Hidden on other turns; kept on (a setting)
// until the button turns it off.

// The button.
void tacticalViewToggle();
bool tacticalViewIsOn();

// On and the player's turn: the view shows and picks tiles.
bool tacticalViewIsShown();

// Game loop: off when the combat is over, see-through drawing while shown.
void tacticalViewUpdate();

// How a critter's tile is drawn: [color] - palette entry of the game's
// combat outline (cycled as it is), [thick] - the dude's and the selected
// enemy's; false - not drawn (the game doesn't see it: no outline).
bool tacticalViewTileLook(Object* critter, int* color, bool* thick);

// Living critter standing on [tile] (shown on the map), nullptr - none.
Object* tacticalViewCritterAt(int tile);

// Where the dude can walk now: tiles reachable with the action points left
// (and the combat's free move), the dude's tile not included.
struct TacticalViewReach {
    std::vector<int> reachable;
};
const TacticalViewReach& tacticalViewGetReach();

} // namespace fallout

#endif /* FALLOUT_TACTICAL_VIEW_H_ */
