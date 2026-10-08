#include "tactical_view.h"

#include <SDL.h>

#include <algorithm>
#include <deque>
#include <unordered_map>

#include "art.h"
#include "combat.h"
#include "critter.h"
#include "game.h"
#include "map.h"
#include "object.h"
#include "tile.h"
#include "touch_controls.h"

namespace fallout {

namespace {

    // Picked again this often while nothing it depends on changes (critters
    // and doors move on their own).
    constexpr unsigned int kReachRefreshMs = 250;

    bool gOn = false;

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
        gReach.blocked.clear();

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

                Object* blocker = _obj_blocking_at(gDude, neighbour, gDude->elevation);
                if (blocker != nullptr) {
                    if (FrmId(blocker).objectType() != OBJ_TYPE_CRITTER) {
                        gReach.blocked.push_back(neighbour);
                    }
                    continue;
                }

                gReach.reachable.push_back(neighbour);
                queue.push_back(neighbour);
            }
        }
    }

} // namespace

void tacticalViewToggle()
{
    gOn = !gOn && isInCombat();
}

bool tacticalViewIsOn()
{
    return gOn;
}

bool tacticalViewIsShown()
{
    return gOn
        && gDude != nullptr
        && isInCombat()
        && (GameMode::getCurrentGameMode() & GameMode::kPlayerTurn) != 0
        && touchControlsIsEnabled();
}

void tacticalViewUpdate()
{
    if (!isInCombat()) {
        gOn = false;
    }

    objectSetSeeThrough(tacticalViewIsShown());
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
