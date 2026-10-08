#ifndef FALLOUT_PLAYER_COMMANDS_H_
#define FALLOUT_PLAYER_COMMANDS_H_

#include "obj_types.h"
#include "skill_defs.h"

namespace fallout {

// CE: Commands of the player character with explicit targets - objects and
// tiles, never the mouse cursor. The mouse (game_mouse.cc) and touch controls
// (touch_controls.cc) are front ends of these; the commands call the game's
// own actions, so rules (action points, reach, scripts) don't depend on the
// input device. See docs/touch-architecture.md.
//
// Object actions of the action menu are `gameMouseBuildActionMenuItems` /
// `gameMouseExecuteActionMenuItem` (object based too).

// Object at a map point (world view coordinates, see world_view.h) as the
// game picks it under the mouse: the topmost of [objectType] (-1 - any),
// roofs covering the point hide objects under them.
Object* playerObjectAt(int worldX, int worldY, ObjectType objectType, bool includeDude, int elevation);

// Object the primary action at a map point goes to: the object there, or an
// outlined item behind a wall, scenery or misc object.
Object* playerPrimaryTargetAt(int worldX, int worldY, int elevation);

// Primary action on an object (left click in arrow mode): pick up an item,
// use scenery, talk to or loot a critter, turn dude, look at the rest.
void playerPrimaryAction(Object* target);

// Walks to [tile] (the same tile twice in a row runs), or runs if running is
// the preference; [alternate] (Shift) swaps walking and running. In combat
// uses the action points dude has.
int playerMoveTo(int tile, bool alternate);

// Attacks [target] with the active weapon and attack mode (combat starts if
// needed).
void playerAttack(Object* target);

// Uses the active item on [target] (throwing, explosives), in combat for its
// action points.
void playerUseActiveItemOn(Object* target);

// Uses [skill] on [target].
int playerUseSkillOn(Skill skill, Object* target);

// Examines [target] (description), or looks at it.
void playerLook(Object* target);

// Chance to hit [target] with the active attack mode; false when it can't be
// hit (out of range, no line of fire).
bool playerGetHitChance(Object* target, int* chance);

// Action points moving to [tile] takes in combat, -1 when there's no path.
int playerGetMoveCost(int tile);

} // namespace fallout

#endif /* FALLOUT_PLAYER_COMMANDS_H_ */
