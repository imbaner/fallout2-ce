#include "tactical_view.h"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
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
    const Color kFriendlyColor = Color(229); // slime
    const Color kHostileColor = Color(243); // fire_fast
    const Color kBlockedColor = Color(61);
    const Color kTargetColor = Color(254); // bobber

    // What the tiles drawn under the objects show; the map is drawn again
    // when it changes.
    std::size_t gTilesSignature = 0;

    // A critter's tile color and line width (world pixels); false - not
    // drawn (the game doesn't see it: no outline).
    bool tileLook(Object* critter, Color* color, int* width)
    {
        *width = 2;
        if (critter == gDude) {
            *color = kFriendlyColor;
            *width = 3;
            return true;
        }
        if (critter == mapHintsGetAttackTarget()) {
            *color = kTargetColor;
            *width = 3;
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

    bool isShownCritter(Object* object, int elevation)
    {
        return FrmId(object).objectType() == OBJ_TYPE_CRITTER
            && object->elevation == elevation
            && (object->flags & OBJECT_HIDDEN) == 0
            && !critterIsDead(object);
    }

    // A line [width] pixels thick (thickened down: the edges are mostly
    // across) clipped to [rect].
    void drawLine(unsigned char* buffer, int pitch, const Rect& rect, int x0, int y0, int x1, int y1, int width, Color color)
    {
        int dx = std::abs(x1 - x0);
        int dy = -std::abs(y1 - y0);
        int stepX = x0 < x1 ? 1 : -1;
        int stepY = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        while (true) {
            for (int offset = 0; offset < width; offset++) {
                int y = y0 + offset - width / 2;
                if (x0 >= rect.left && x0 <= rect.right && y >= rect.top && y <= rect.bottom) {
                    buffer[pitch * y + x0] = color;
                }
            }
            if (x0 == x1 && y0 == y1) {
                break;
            }
            int doubled = 2 * error;
            if (doubled >= dy) {
                error += dy;
                x0 += stepX;
            }
            if (doubled <= dx) {
                error += dx;
                y0 += stepY;
            }
        }
    }

    // Center of [tile] in the game's buffer.
    bool tileCenter(int tile, int* x, int* y)
    {
        if (!tileIsValid(tile) || tileToScreenXY(tile, x, y) != 0) {
            return false;
        }
        *x += 16;
        *y += 8;
        return true;
    }

    // Everyone's tile under them (`objectSetSeeThroughUnderlay`): a corner
    // of a hex is the middle of its center and two neighbours' (as the map
    // hints' outlines).
    void drawTiles(unsigned char* buffer, int pitch, const Rect& rect, int elevation)
    {
        for (Object* object = objectFindFirstAtElevation(elevation); object != nullptr; object = objectFindNextAtElevation()) {
            Color color;
            int width;
            if (!isShownCritter(object, elevation) || !tileLook(object, &color, &width)) {
                continue;
            }

            int centerX;
            int centerY;
            if (!tileCenter(object->tile, &centerX, &centerY)) {
                continue;
            }

            int neighbourX[ROTATION_COUNT];
            int neighbourY[ROTATION_COUNT];
            bool complete = true;
            for (int rotation = 0; rotation < ROTATION_COUNT && complete; rotation++) {
                complete = tileCenter(tileGetTileInDirection(object->tile, static_cast<Rotation>(rotation), 1), &(neighbourX[rotation]), &(neighbourY[rotation]));
            }
            if (!complete) {
                continue;
            }

            int cornerX[ROTATION_COUNT];
            int cornerY[ROTATION_COUNT];
            for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
                int next = (rotation + 1) % ROTATION_COUNT;
                cornerX[rotation] = (centerX + neighbourX[rotation] + neighbourX[next]) / 3;
                cornerY[rotation] = (centerY + neighbourY[rotation] + neighbourY[next]) / 3;
            }
            for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
                int next = (rotation + 1) % ROTATION_COUNT;
                drawLine(buffer, pitch, rect, cornerX[rotation], cornerY[rotation], cornerX[next], cornerY[next], width, color);
            }
        }
    }

    // What the tiles show now (who stands where, how the game sees them,
    // the selected enemy).
    std::size_t tilesSignature()
    {
        std::size_t signature = std::hash<const void*>()(mapHintsGetAttackTarget());
        for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
            Color color;
            int width;
            if (isShownCritter(object, gElevation) && tileLook(object, &color, &width)) {
                signature = signature * 31 + static_cast<std::size_t>(object->tile) * 7 + color * 3 + width;
            }
        }
        return signature;
    }

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
    return settings.touch.tactical_view
        && gDude != nullptr
        && isInCombat()
        && (GameMode::getCurrentGameMode() & GameMode::kPlayerTurn) != 0
        && touchControlsIsEnabled();
}

void tacticalViewUpdate()
{
    objectSetSeeThroughUnderlay(drawTiles);

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

    bool wasShown = gTilesSignature != 0;
    objectSetSeeThrough(shown);
    if (!shown) {
        gTilesSignature = 0;
        return;
    }

    // The tiles drawn with the map change: the map is drawn again (when it
    // just showed, `objectSetSeeThrough` did).
    std::size_t signature = tilesSignature() | 1;
    if (signature != gTilesSignature) {
        if (wasShown) {
            tileWindowRefresh();
        }
        gTilesSignature = signature;
    }
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
