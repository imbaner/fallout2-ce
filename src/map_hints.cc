#include "map_hints.h"

#include <algorithm>
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

    // Palette entries of the game's combat outlines (object.cc): they cycle
    // (cycle.cc), the tiles' outlines read them every frame, so they pulse
    // with the critters' outlines.
    constexpr int kFriendlyColor = 229; // slime
    constexpr int kHostileColor = 243; // fire_fast
    constexpr int kBlockedColor = 61;
    constexpr int kTargetColor = 254; // bobber

    MuiColor paletteColor(int index, Uint8 alpha = 255)
    {
        const unsigned char* palette = directDrawGetPalette();
        return { static_cast<Uint8>(palette[index * 3] << 2), static_cast<Uint8>(palette[index * 3 + 1] << 2), static_cast<Uint8>(palette[index * 3 + 2] << 2), alpha };
    }

    void drawTacticalView(MuiContext& ui, float scale)
    {
        MuiRect screen = ui.screenRect();
        auto onScreen = [&](const std::vector<SDL_FPoint>& outline) {
            return !outline.empty() && outline[0].x > screen.x - screen.w * 0.1f && outline[0].x < screen.right() + screen.w * 0.1f
                && outline[0].y > screen.y - screen.h * 0.1f && outline[0].y < screen.bottom() + screen.h * 0.1f;
        };

        // Where the dude can walk: each tile lightly filled, its grid.
        const TacticalViewReach& reach = tacticalViewGetReach();
        MuiColor fill = muiTheme().accent.withAlpha(34);
        MuiColor grid = muiTheme().accent.withAlpha(70);
        for (int tile : reach.reachable) {
            std::vector<SDL_FPoint> outline = tileOutline(tile, scale);
            if (onScreen(outline)) {
                muiFillConvex(outline, fill);
                muiDrawPolyline(outline, ui.dp(1.0f), grid, true);
            }
        }

        // Everyone's tile in the color the game outlines them with in
        // combat; the dude's thicker, the selected enemy pulsing.
        for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
            if (FrmId(object).objectType() != OBJ_TYPE_CRITTER || (object->flags & OBJECT_HIDDEN) != 0 || critterIsDead(object)) {
                continue;
            }

            int index;
            float width = 1.6f;
            if (object == gDude) {
                index = kFriendlyColor;
                width = 2.6f;
            } else if (object == gAttackTarget) {
                index = kTargetColor;
                width = 2.6f;
            } else {
                switch (object->outline & OUTLINE_TYPE_MAX) {
                case OUTLINE_TYPE_HOSTILE:
                    index = kHostileColor;
                    break;
                case OUTLINE_TYPE_FRIENDLY:
                case OUTLINE_TYPE_SAME_TEAM:
                    index = kFriendlyColor;
                    break;
                case OUTLINE_TYPE_BLOCKED:
                    index = kBlockedColor;
                    break;
                default:
                    // Not seen (no outline in the game either).
                    continue;
                }
            }

            std::vector<SDL_FPoint> outline = tileOutline(object->tile, scale);
            if (onScreen(outline)) {
                muiDrawPolyline(outline, ui.dp(width), paletteColor(index), true);
            }
        }
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

        bool tactical = tacticalViewIsShown();
        if (tactical) {
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

void mapHintsClearSelection()
{
    setAttackTarget(nullptr);
    gUseTarget = nullptr;
    gMoveTile = -1;
}

} // namespace fallout
