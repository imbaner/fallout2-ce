#ifndef FALLOUT_MAP_HINTS_H_
#define FALLOUT_MAP_HINTS_H_

#include "obj_types.h"

namespace fallout {

// CE: Hints of the touch controls over the map, anchored to objects and
// tiles and drawn by the mobile UI at a fixed size (they don't zoom with the
// map): hit chance above the enemy selected in combat, the tile selected in
// combat with the action points moving there takes, the tile dude walks to.
// They replace the game's cursor, which touch controls don't have (see
// docs/touch-architecture.md).

void mapHintsInit();

// Enemy selected for an attack (hit chance above it) or an object selected
// for use in combat (outlined); nullptr - none.
void mapHintsSetAttackTarget(Object* target);
void mapHintsSetUseTarget(Object* target);

// Object dude acts on (picks up, talks to, uses): outlined green until dude
// is done with it; nullptr - none.
void mapHintsSetInteraction(Object* object);

// Tile selected for a move in combat, -1 - none.
void mapHintsSetMoveTile(int tile);

// Tile dude walks to, the mark goes away when dude stops; -1 - none.
void mapHintsSetDestination(int tile);
int mapHintsGetDestination();

void mapHintsClearSelection();

// The enemy selected for an attack, nullptr - none.
Object* mapHintsGetAttackTarget();

} // namespace fallout

#endif /* FALLOUT_MAP_HINTS_H_ */
