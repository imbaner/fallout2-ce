#include "tactical_view.h"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <unordered_map>

#include "art.h"
#include "combat.h"
#include "critter.h"
#include "game.h"
#include "game_config.h"
#include "game_mouse.h"
#include "map.h"
#include "map_hints.h"
#include "object.h"
#include "settings.h"
#include "tile.h"
#include "touch_controls.h"

namespace fallout {

namespace {

    // Picked again this often while nothing it depends on changes (critters
    // and doors move on their own).
    constexpr unsigned int kReachRefreshMs = 250;

    // The outlines' colors are the game's sight of each critter, kept up to
    // date while shown: from the dude's tile when it changes.
    int gOutlinesTile = -1;
    bool gOutlinesEnabled = false;

    void updateOutline(Object* critter)
    {
        _combat_update_critter_outline_for_los(critter, gOutlinesEnabled);
    }

    struct ReachKey {
        int tile = -1;
        int elevation = -1;
        int actionPoints = -1;
        int freeMove = -1;

        bool operator==(const ReachKey& other) const
        {
            return tile == other.tile && elevation == other.elevation && actionPoints == other.actionPoints && freeMove == other.freeMove;
        }
    };

    // Palette entries of the game's combat outlines (object.cc), cycled by
    // the palette (cycle.cc): tiles drawn with them pulse as outlines do.
    constexpr int kFriendlyColor = 229; // slime
    constexpr int kHostileColor = 243; // fire_fast
    constexpr int kBlockedColor = 61;
    constexpr int kTargetColor = 254; // bobber

    TacticalViewReach gReach;
    ReachKey gReachKey;
    unsigned int gReachTime = 0;

    // Steps the dude can walk with the action points left, as
    // `playerGetMoveCost` charges them (crippled legs, the free move).
    int maxSteps()
    {
        int actionPoints = gDude->data.critter.combat.ap;
        int steps = 0;
        while (steps < 100) {
            int cost = std::max(0, critterGetMovementPointCostAdjustedForCrippledLegs(gDude, steps + 1) - _combat_free_move);
            if (cost > actionPoints) {
                break;
            }
            steps++;
        }
        return steps;
    }

    // Steps over the hex grid from the dude's tile around what blocks the
    // way, as the game's path finding does (`_make_path`).
    void computeReach()
    {
        gReach.reachable.clear();

        int steps = maxSteps();
        if (steps == 0) {
            return;
        }

        std::unordered_map<int, int> distances;
        std::deque<int> queue;
        distances[gDude->tile] = 0;
        queue.push_back(gDude->tile);
        while (!queue.empty()) {
            int tile = queue.front();
            queue.pop_front();
            int distance = distances[tile];
            if (distance >= steps) {
                continue;
            }

            for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
                int neighbour = tileGetTileInDirection(tile, static_cast<Rotation>(rotation), 1);
                if (!tileIsValid(neighbour) || distances.count(neighbour) != 0) {
                    continue;
                }
                distances[neighbour] = distance + 1;

                if (_obj_blocking_at(gDude, neighbour, gDude->elevation) != nullptr) {
                    continue;
                }

                gReach.reachable.push_back(neighbour);
                queue.push_back(neighbour);
            }
        }
    }

} // namespace

bool tacticalViewTileLook(Object* critter, int* color, bool* thick)
{
    *thick = false;
    if (critter == gDude) {
        *color = kFriendlyColor;
        *thick = true;
        return true;
    }
    if (critter == mapHintsGetAttackTarget()) {
        *color = kTargetColor;
        *thick = true;
        return true;
    }
    switch (critter->outline & OUTLINE_TYPE_MAX) {
    case OUTLINE_TYPE_HOSTILE:
        *color = kHostileColor;
        return true;
    case OUTLINE_TYPE_FRIENDLY:
    case OUTLINE_TYPE_SAME_TEAM:
        *color = kFriendlyColor;
        return true;
    case OUTLINE_TYPE_BLOCKED:
        *color = kBlockedColor;
        return true;
    default:
        return false;
    }
}

void tacticalViewToggle()
{
    settings.touch.tactical_view = !settings.touch.tactical_view;
}

bool tacticalViewIsOn()
{
    return settings.touch.tactical_view;
}

bool tacticalViewIsShown()
{
    return settings.enhancements.tactical_view
        && settings.touch.tactical_view
        && gDude != nullptr
        && isInCombat()
        && (GameMode::getCurrentGameMode() & GameMode::kPlayerTurn) != 0
        && touchControlsIsEnabled();
}

void tacticalViewUpdate()
{
    bool shown = tacticalViewIsShown();
    if (!shown) {
        gOutlinesTile = -1;
    } else if (gDude->tile != gOutlinesTile) {
        // Who the dude sees (as `_combat_outline_on`), enabled only where
        // the game shows them now - the view draws them anyway.
        gOutlinesTile = gDude->tile;
        gOutlinesEnabled = settings.preferences.target_highlight != TARGET_HIGHLIGHT_OFF
            && (combatOutlinesFollowTurn() || gameMouseGetMode() == GAME_MOUSE_MODE_CROSSHAIR);
        combatForEachCritter(updateOutline);
    }

    objectSetSeeThrough(shown);
}

Object* tacticalViewCritterAt(int tile)
{
    if (!tileIsValid(tile)) {
        return nullptr;
    }

    for (Object* object = objectFindFirstAtLocation(gElevation, tile); object != nullptr; object = objectFindNextAtLocation()) {
        if (FrmId(object).objectType() == OBJ_TYPE_CRITTER
            && (object->flags & OBJECT_HIDDEN) == 0
            && !critterIsDead(object)) {
            return object;
        }
    }
    return nullptr;
}

const TacticalViewReach& tacticalViewGetReach()
{
    if (gDude == nullptr || !isInCombat()) {
        gReach = TacticalViewReach();
        gReachKey = ReachKey();
        return gReach;
    }

    ReachKey key = { gDude->tile, gDude->elevation, gDude->data.critter.combat.ap, _combat_free_move };
    unsigned int now = SDL_GetTicks();
    if (!(key == gReachKey) || now - gReachTime >= kReachRefreshMs) {
        gReachKey = key;
        gReachTime = now;
        computeReach();
    }
    return gReach;
}

} // namespace fallout
