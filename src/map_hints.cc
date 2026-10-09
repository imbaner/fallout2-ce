#include "map_hints.h"

#include <algorithm>
#include <unordered_set>
#include <string>
#include <vector>

#include "animation.h"
#include "combat.h"
#include "critter.h"
#include "game.h"
#include "map.h"
#include "mui.h"
#include "mui_draw.h"
#include "mui_icons.h"
#include "mui_screens.h"
#include "object.h"
#include "player_commands.h"
#include "svga.h"
#include "tactical_view.h"
#include "tile.h"
#include "touch_controls.h"
#include "touch_hud.h"
#include "window_manager.h"
#include "world_view.h"

namespace fallout {

namespace {

    const MuiColor kDanger = muiRgb(0xE0584F);
    const MuiColor kPill = muiRgb(0x0B120D, 230);

    // Game modes of screens covering the map (as for the HUD).
    constexpr int kScreenGameModes = ~(GameMode::kCombat | GameMode::kPlayerTurn | GameMode::kSpecial | GameMode::kUseOn);

    Object* gAttackTarget = nullptr;
    Object* gUseTarget = nullptr;
    int gMoveTile = -1;
    int gDestination = -1;

    // Object dude interacts with, outlined green (the game's outline) until
    // dude is done; its own outline is restored then.
    Object* gInteraction = nullptr;
    OutlineType gInteractionSavedOutline = OUTLINE_TYPE_NONE;

    // Move cost of `gMoveTile`, computed again when dude moves or spends
    // action points (pathfinding is too slow for every frame).
    struct MoveCost {
        int tile = -1;
        int from = -1;
        int actionPoints = -1;
        int cost = -1;
    };
    MoveCost gMoveCost;

    bool objectExists(Object* object)
    {
        for (Object* candidate = objectFindFirstAtElevation(gElevation); candidate != nullptr; candidate = objectFindNextAtElevation()) {
            if (candidate == object) {
                return true;
            }
        }
        return false;
    }

    // The attack target: its outline pulses (`objectSetTargetOutline`), the
    // game's buffer is drawn again where it changes.
    void setAttackTarget(Object* target)
    {
        if (target == gAttackTarget) {
            return;
        }

        Object* previous = gAttackTarget;
        gAttackTarget = target;
        objectSetTargetOutline(target);
        for (Object* object : { previous, target }) {
            if (object != nullptr && objectExists(object)) {
                Rect rect;
                objectGetRect(object, &rect);
                tileWindowRefreshRect(&rect, object->elevation);
            }
        }
    }

    void clearInteraction()
    {
        if (gInteraction != nullptr && objectExists(gInteraction)) {
            // The rect while outlined covers the outline too.
            Rect rect;
            objectGetRect(gInteraction, &rect);
            gInteraction->outline = gInteractionSavedOutline;
            tileWindowRefreshRect(&rect, gInteraction->elevation);
        }
        gInteraction = nullptr;
    }

    // Center of [tile] in output pixels.
    bool tileCenter(int tile, float scale, SDL_FPoint* point)
    {
        int worldX;
        int worldY;
        if (tileToScreenXY(tile, &worldX, &worldY) != 0) {
            return false;
        }

        int screenX;
        int screenY;
        worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
        *point = { screenX * scale, screenY * scale };
        return true;
    }

    // Outline of [tile]: a corner of a hex is where it meets two neighbours,
    // the middle of the three centers, so it matches the game's grid at any
    // zoom.
    std::vector<SDL_FPoint> tileOutline(int tile, float scale)
    {
        SDL_FPoint center;
        if (!tileCenter(tile, scale, &center)) {
            return {};
        }

        SDL_FPoint neighbours[ROTATION_COUNT];
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            int neighbour = tileGetTileInDirection(tile, static_cast<Rotation>(rotation), 1);
            if (neighbour == -1 || !tileCenter(neighbour, scale, &(neighbours[rotation]))) {
                return {};
            }
        }

        std::vector<SDL_FPoint> points;
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            const SDL_FPoint& a = neighbours[rotation];
            const SDL_FPoint& b = neighbours[(rotation + 1) % ROTATION_COUNT];
            points.push_back({ (center.x + a.x + b.x) / 3.0f, (center.y + a.y + b.y) / 3.0f });
        }
        return points;
    }

    // MARK: Tactical view

    MuiColor paletteColor(int index, Uint8 alpha = 255)
    {
        const unsigned char* palette = directDrawGetPalette();
        return { static_cast<Uint8>(palette[index * 3] << 2), static_cast<Uint8>(palette[index * 3 + 1] << 2), static_cast<Uint8>(palette[index * 3 + 2] << 2), alpha };
    }

    // The outlines and the dude drawn again over the tiles, pixel for pixel
    // (`objectSeeThroughTopRuns`).
    void drawTopLayer(MuiContext& ui, float scale)
    {
        unsigned int version;
        const std::vector<ObjectTopRun>* runs = objectSeeThroughTopRuns(&version);
        SDL_Renderer* renderer = muiDrawGetRenderer();
        if (runs == nullptr || runs->empty() || renderer == nullptr) {
            return;
        }

        // World to screen is a scale and an offset.
        float originX;
        float originY;
        float unitX;
        float unitY;
        worldViewWorldToScreenF(0.0f, 0.0f, &originX, &originY);
        worldViewWorldToScreenF(1.0f, 1.0f, &unitX, &unitY);
        float zoomX = (unitX - originX) * scale;
        float zoomY = (unitY - originY) * scale;
        originX *= scale;
        originY *= scale;

        MuiRect screen = ui.screenRect();
        const unsigned char* palette = directDrawGetPalette();
        static std::vector<SDL_Vertex> vertices;
        static std::vector<int> indices;
        vertices.clear();
        indices.clear();
        for (const ObjectTopRun& run : *runs) {
            float left = originX + run.x * zoomX;
            float top = originY + run.y * zoomY;
            float right = left + run.length * zoomX;
            float bottom = top + zoomY;
            if (right < screen.x || left > screen.right() || bottom < screen.y || top > screen.bottom()) {
                continue;
            }

            SDL_Color color = { static_cast<Uint8>(palette[run.color * 3] << 2), static_cast<Uint8>(palette[run.color * 3 + 1] << 2), static_cast<Uint8>(palette[run.color * 3 + 2] << 2), 255 };
            int first = static_cast<int>(vertices.size());
            vertices.push_back({ { left, top }, color, { 0.0f, 0.0f } });
            vertices.push_back({ { right, top }, color, { 0.0f, 0.0f } });
            vertices.push_back({ { right, bottom }, color, { 0.0f, 0.0f } });
            vertices.push_back({ { left, bottom }, color, { 0.0f, 0.0f } });
            indices.insert(indices.end(), { first, first + 1, first + 2, first, first + 2, first + 3 });
        }
        if (!indices.empty()) {
            SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
        }
    }

    // The combat's living critters (all on the map), the dude among them.
    std::vector<Object*> gCombatCritters;

    void addCombatCritter(Object* critter)
    {
        gCombatCritters.push_back(critter);
    }

    // Under the selected tile and the hit chance: a faint grid where the
    // dude can walk, the area's border, everyone's tile in the color the
    // game outlines them with in combat (the dude's and the selected
    // enemy's thicker), then the outlines and the dude over all of it.
    void drawTacticalView(MuiContext& ui, float scale)
    {
        const TacticalViewReach& reach = tacticalViewGetReach();
        if (!reach.reachable.empty()) {
            std::unordered_set<int> area(reach.reachable.begin(), reach.reachable.end());
            area.insert(gDude->tile);

            // Fainter zoomed out (tiles get small).
            float zoom = std::clamp(worldViewGetZoom(), 0.5f, 1.0f);
            MuiColor grid = muiRgb(0xD9C79A, static_cast<Uint8>(22.0f + 26.0f * (zoom - 0.5f) * 2.0f));
            MuiColor border = muiTheme().accent.withAlpha(190);
            for (int pass = 0; pass < 2; pass++) {
                for (int tile : area) {
                    std::vector<SDL_FPoint> outline = tileOutline(tile, scale);
                    if (outline.empty()) {
                        continue;
                    }
                    // Corner `k` is between the neighbours `k` and `k + 1`,
                    // the edge towards neighbour `d` from corner `d - 1` to
                    // `d`. An edge inside is drawn by the tile on its side
                    // towards neighbours 0-2, once.
                    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
                        bool inside = area.count(tileGetTileInDirection(tile, static_cast<Rotation>(rotation), 1)) != 0;
                        if (pass == 0 ? !inside || rotation >= 3 : inside) {
                            continue;
                        }
                        const SDL_FPoint& from = outline[(rotation + ROTATION_COUNT - 1) % ROTATION_COUNT];
                        const SDL_FPoint& to = outline[rotation];
                        if (pass == 0) {
                            muiDrawLine(from.x, from.y, to.x, to.y, ui.dp(1.0f), grid);
                        } else {
                            muiDrawLine(from.x, from.y, to.x, to.y, ui.dp(1.6f), border);
                        }
                    }
                }
            }
        }

        gCombatCritters.clear();
        gCombatCritters.push_back(gDude);
        combatForEachCritter(addCombatCritter);
        for (Object* object : gCombatCritters) {
            if (object->elevation != gElevation || (object->flags & OBJECT_HIDDEN) != 0) {
                continue;
            }

            int color;
            bool thick;
            if (!tacticalViewTileLook(object, &color, &thick)) {
                continue;
            }

            std::vector<SDL_FPoint> outline = tileOutline(object->tile, scale);
            if (!outline.empty()) {
                muiDrawPolyline(outline, ui.dp(thick ? 2.6f : 1.6f), paletteColor(color), true);
            }
        }

        drawTopLayer(ui, scale);
    }

    float outlineRight(const std::vector<SDL_FPoint>& points)
    {
        float right = points.front().x;
        for (const SDL_FPoint& point : points) {
            right = std::max(right, point.x);
        }
        return right;
    }

    // Small label: optional icon, optional text.
    void drawPill(MuiContext& ui, float x, float centerY, bool centered, MuiIcon icon, bool hasIcon, const std::u32string& text, MuiColor color, MuiColor textColor)
    {
        float size = ui.dp(13.0f);
        float height = ui.dp(22.0f);
        float padding = ui.dp(7.0f);
        float iconSize = ui.dp(13.0f);
        float textWidth = text.empty() ? 0.0f : muiTextWidth(text, size);
        float width = padding * 2.0f + (hasIcon ? iconSize : 0.0f) + (hasIcon && !text.empty() ? ui.dp(3.0f) : 0.0f) + textWidth;

        MuiRect rect = { centered ? x - width / 2.0f : x, centerY - height / 2.0f, width, height };
        muiFillRoundRect(rect, height / 2.0f, kPill);
        muiStrokeRoundRect(rect, height / 2.0f, ui.dp(1.0f), color.withAlpha(200));

        float cursor = rect.x + padding;
        if (hasIcon) {
            muiDrawIcon(icon, cursor + iconSize / 2.0f, rect.centerY(), iconSize, ui.dp(1.5f), color);
            cursor += iconSize + ui.dp(3.0f);
        }
        if (!text.empty()) {
            muiDrawTextAligned(text, { cursor, rect.y, textWidth + 1.0f, rect.h }, size, textColor, MuiAlign::Start, MuiAlign::Center);
        }
    }

    std::u32string toText(const std::string& text)
    {
        return std::u32string(text.begin(), text.end());
    }

    class MapHintsScreen : public MuiScreen {
    public:
        MapHintsScreen()
        {
            modal = false;
        }

        bool isActive() override
        {
            validate();

            bool anything = gAttackTarget != nullptr || gMoveTile != -1 || gDestination != -1 || tacticalViewIsShown();
            return anything
                && touchControlsIsEnabled()
                && (GameMode::getCurrentGameMode() & kScreenGameModes) == 0
                && !windowIsModalShown();
        }

        void build(MuiContext& ui) override;

    private:
        void validate();
    };

    MapHintsScreen gMapHintsScreen;
    bool gMapHintsPushed = false;

    void MapHintsScreen::validate()
    {
        if (gDude == nullptr) {
            mapHintsClearSelection();
            gDestination = -1;
            return;
        }

        // Selection is for combat only.
        if (!isInCombat()) {
            mapHintsClearSelection();
        }

        if (gAttackTarget != nullptr && (!objectExists(gAttackTarget) || critterIsDead(gAttackTarget))) {
            setAttackTarget(nullptr);
        }

        if (gUseTarget != nullptr && !objectExists(gUseTarget)) {
            gUseTarget = nullptr;
        }

        if (gDestination != -1 && (gDude->tile == gDestination || !animationIsBusy(gDude))) {
            gDestination = -1;
        }

        // Selected for use in combat, or dude still walking to / acting on
        // it.
        if (gInteraction != nullptr && gInteraction != gUseTarget && !animationIsBusy(gDude)) {
            clearInteraction();
        }
    }

    void MapHintsScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        float scale = ui.screenRect().w / screenGetWidth();

        if (tacticalViewIsShown()) {
            drawTacticalView(ui, scale);
        }

        if (gDestination != -1) {
            std::vector<SDL_FPoint> outline = tileOutline(gDestination, scale);
            if (!outline.empty()) {
                muiDrawPolyline(outline, ui.dp(1.5f), theme.accent.withAlpha(170), true);
            }
        }

        if (gMoveTile != -1) {
            std::vector<SDL_FPoint> outline = tileOutline(gMoveTile, scale);
            if (!outline.empty()) {
                int actionPoints = gDude->data.critter.combat.ap;
                if (gMoveCost.tile != gMoveTile || gMoveCost.from != gDude->tile || gMoveCost.actionPoints != actionPoints) {
                    gMoveCost = { gMoveTile, gDude->tile, actionPoints, playerGetMoveCost(gMoveTile) };
                }

                bool reachable = gMoveCost.cost != -1 && gMoveCost.cost <= actionPoints;
                MuiColor color = reachable ? theme.accent : kDanger;
                muiFillConvex(outline, color.withAlpha(45));
                muiDrawPolyline(outline, ui.dp(1.5f), color, true);

                // Cost inside the tile, with a shadow to read it over the
                // ground.
                SDL_FPoint center;
                tileCenter(gMoveTile, scale, &center);
                if (gMoveCost.cost == -1) {
                    muiDrawIcon(MuiIcon::Cancel, center.x, center.y, ui.dp(12.0f), ui.dp(2.0f), kDanger);
                } else {
                    std::u32string text = toText(std::to_string(gMoveCost.cost));
                    float size = ui.dp(14.0f);
                    MuiRect textRect = { center.x - ui.dp(20.0f), center.y - ui.dp(10.0f), ui.dp(40.0f), ui.dp(20.0f) };
                    MuiRect shadowRect = { textRect.x + ui.dp(1.0f), textRect.y + ui.dp(1.0f), textRect.w, textRect.h };
                    muiDrawTextAligned(text, shadowRect, size, muiRgb(0x000000, 220), MuiAlign::Center, MuiAlign::Center);
                    muiDrawTextAligned(text, textRect, size, reachable ? theme.text : kDanger, MuiAlign::Center, MuiAlign::Center);
                }
            }
        }

        // Hit chance above the enemy (the tactical view too: there the enemy
        // is picked by its tile).
        if (gAttackTarget != nullptr) {
            SDL_FPoint pill;
            MuiRect rect;
            bool placed = false;
            if (muiObjectRect(gAttackTarget, &rect)) {
                pill = { rect.centerX(), rect.y - ui.dp(14.0f) };
                placed = true;
            }

            int chance;
            if (placed && playerGetHitChance(gAttackTarget, &chance)) {
                // No chance: dashes, like the game's called shot window.
                drawPill(ui, pill.x, pill.y, true, MuiIcon::Aim, false, toText(chance >= 0 ? std::to_string(chance) + "%" : "--"), theme.accent, theme.text);
            } else if (placed) {
                drawPill(ui, pill.x, pill.y, true, MuiIcon::Cancel, true, std::u32string(), kDanger, kDanger);
            }
        }
    }

} // namespace

void mapHintsInit()
{
    if (!gMapHintsPushed) {
        // Under the HUD (screens draw in the order they were pushed).
        muiPush(&gMapHintsScreen);
        gMapHintsPushed = true;
    }
}

void mapHintsSetAttackTarget(Object* target)
{
    clearInteraction();
    setAttackTarget(target);
    gUseTarget = nullptr;
    gMoveTile = -1;
}

void mapHintsSetUseTarget(Object* target)
{
    gUseTarget = target;
    setAttackTarget(nullptr);
    gMoveTile = -1;
    mapHintsSetInteraction(target);
}

void mapHintsSetInteraction(Object* object)
{
    if (object == gInteraction) {
        return;
    }

    clearInteraction();
    if (object == nullptr || object == gDude) {
        return;
    }

    // Set directly: the game's outline is drawn for any object, "no
    // highlight" objects (shelves, many containers and doors) are only kept
    // out of the game's own highlighting.
    // The highlight's outline isn't kept: the highlight outlines the object
    // again if it's still on, and nothing is left if it was switched off.
    gInteraction = object;
    gInteractionSavedOutline = touchHudHighlightOwns(object) ? OUTLINE_TYPE_NONE : object->outline;
    object->outline = OUTLINE_TYPE_FRIENDLY;
    if ((object->flags & OBJECT_HIDDEN) != 0) {
        object->outline = static_cast<OutlineType>(object->outline | OUTLINE_DISABLED);
    }

    Rect rect;
    objectGetRect(object, &rect);
    tileWindowRefreshRect(&rect, object->elevation);
}

void mapHintsSetMoveTile(int tile)
{
    clearInteraction();
    gMoveTile = tile;
    setAttackTarget(nullptr);
    gUseTarget = nullptr;
}

void mapHintsSetDestination(int tile)
{
    gDestination = tile;
}

int mapHintsGetDestination()
{
    return gDestination;
}

Object* mapHintsGetAttackTarget()
{
    return gAttackTarget;
}

void mapHintsClearSelection()
{
    setAttackTarget(nullptr);
    gUseTarget = nullptr;
    gMoveTile = -1;
}

} // namespace fallout
