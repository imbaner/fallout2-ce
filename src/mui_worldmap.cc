#include "mui.h"
#include "mui_icons.h"
#include "mui_screens.h"

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "art.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_sound.h"
#include "scripts.h"
#include "input.h"
#include "kb.h"
#include "settings.h"
#include "svga.h"
#include "worldmap.h"

namespace fallout {

namespace {

    const MuiColor kFogUnknown = muiRgb(0x000000);
    constexpr unsigned char kFogKnownAlpha = 120;
    const MuiColor kLabelBackground = muiRgb(0x000000, 150);
    const MuiColor kDestination = muiRgb(0xFFB000);
    const MuiColor kEncounter = muiRgb(0xFF5A3C);
    const MuiColor kEncounterSpecial = muiRgb(0xFFB000);
    const MuiColor kFuel = muiRgb(0x3FA04E);

    // Texts in `game\ce.msg`.
    constexpr int kTextTowns = 315;
    constexpr int kTextHere = 316;
    constexpr int kTextDestination = 317;
    constexpr int kTextVisited = 318;
    constexpr int kTextEnter = 319;
    constexpr int kTextEnterWasteland = 320;
    constexpr int kTextGas = 321;

    // A finger's target around small marks, dp.
    constexpr float kTouchRadius = 26.0f;

    std::u32string gameText(const std::string& text)
    {
        return muiDecodeGameText(text.c_str());
    }

    // The world map as the game's world: its terrain, explored parts, towns,
    // the party and where it goes over the whole screen (a finger moves it, two
    // fingers zoom); the game's date, the car's gas, the menu button (the game
    // menu, `muiWorldmapMenuRun`, which hides them as it hides the touch HUD)
    // and the towns' list float over it; "Enter" shows where the party can
    // enter.
    class WorldmapScreen : public MuiScreen {
    public:
        WorldmapScreen() { modal = true; }

        bool coversScreen() override { return true; }
        bool hidesHud() override { return true; }
        void build(MuiContext& ui) override;
        void back() override;

        MuiWorldmapAction pending;

    private:
        WorldmapMobileState state;
        MuiPanZoom view;
        bool viewReady = false;
        // The camera goes with the party ([enhancements]
        // `worldmap_follow_party`), until the map is moved by a finger.
        bool following = true;
        bool wasWalking = false;
        bool townsOpen = false;

        std::vector<unsigned char> fogSource;
        std::vector<unsigned char> fogPixels;
        unsigned int fogVersion = 0;

        void setupView(const MuiRect& screen, MuiContext& ui);
        void keepInside(const MuiRect& screen);
        void toScreen(const MuiRect& screen, float x, float y, float* screenX, float* screenY) const;

        void drawMap(MuiContext& ui, const MuiRect& screen);
        void drawFog(const MuiRect& screen);
        void drawTowns(MuiContext& ui, const MuiRect& screen);
        void drawParty(MuiContext& ui, const MuiRect& screen);
        void handleMapTap(MuiContext& ui, const MuiRect& screen);

        void buildStatus(MuiContext& ui, const MuiRect& rect);
        MuiRect townsPanelRect(MuiContext& ui, const MuiRect& anchor) const;
        void buildTowns(MuiContext& ui, const MuiRect& panel);
        void buildEnter(MuiContext& ui, const MuiRect& safe);

    public:
        std::u32string areaName(int area) const;
        // Where the party is: the town's name, empty in the wasteland.
        std::u32string partyPlace() const { return areaName(state.currentArea); }

    private:
        void ask(MuiWorldmapActionType type, float x = 0.0f, float y = 0.0f, int area = -1);
    };

    void WorldmapScreen::ask(MuiWorldmapActionType type, float x, float y, int area)
    {
        pending.type = type;
        pending.x = x;
        pending.y = y;
        pending.area = area;
    }

    void WorldmapScreen::back()
    {
        // The towns' list closes; the world map itself is left by entering a
        // place.
        townsOpen = false;
    }

    std::u32string WorldmapScreen::areaName(int area) const
    {
        for (const WorldmapMobileCity& city : state.cities) {
            if (city.area == area) {
                return gameText(city.name);
            }
        }
        return std::u32string();
    }

    void WorldmapScreen::setupView(const MuiRect& screen, MuiContext& ui)
    {
        // The whole screen always shows the map: it's never smaller.
        float cover = std::max(screen.w / std::max(state.width, 1), screen.h / std::max(state.height, 1));
        view.minZoom = cover;
        view.maxZoom = std::max(cover, ui.dp(4.0f));

        if (!viewReady) {
            view.zoom = std::clamp(ui.dp(1.4f), view.minZoom, view.maxZoom);
            view.centerX = state.partyX;
            view.centerY = state.partyY;
            viewReady = true;
        }
        view.zoom = std::clamp(view.zoom, view.minZoom, view.maxZoom);
    }

    void WorldmapScreen::keepInside(const MuiRect& screen)
    {
        float halfWidth = screen.w / 2.0f / view.zoom;
        float halfHeight = screen.h / 2.0f / view.zoom;
        view.centerX = std::clamp(view.centerX, halfWidth, std::max(state.width - halfWidth, halfWidth));
        view.centerY = std::clamp(view.centerY, halfHeight, std::max(state.height - halfHeight, halfHeight));
    }

    void WorldmapScreen::toScreen(const MuiRect& screen, float x, float y, float* screenX, float* screenY) const
    {
        *screenX = screen.centerX() + (x - view.centerX) * view.zoom;
        *screenY = screen.centerY() + (y - view.centerY) * view.zoom;
    }

    void WorldmapScreen::drawMap(MuiContext& ui, const MuiRect& screen)
    {
        // Terrain: the game's art of the tiles in view.
        float left = view.centerX - screen.w / 2.0f / view.zoom;
        float top = view.centerY - screen.h / 2.0f / view.zoom;
        float right = view.centerX + screen.w / 2.0f / view.zoom;
        float bottom = view.centerY + screen.h / 2.0f / view.zoom;

        int rows = state.tilesPerRow > 0 ? static_cast<int>(state.tileFids.size()) / state.tilesPerRow : 0;
        int firstColumn = std::max(static_cast<int>(left / state.tileWidth), 0);
        int lastColumn = std::min(static_cast<int>(right / state.tileWidth), state.tilesPerRow - 1);
        int firstRow = std::max(static_cast<int>(top / state.tileHeight), 0);
        int lastRow = std::min(static_cast<int>(bottom / state.tileHeight), rows - 1);
        for (int row = firstRow; row <= lastRow; row++) {
            for (int column = firstColumn; column <= lastColumn; column++) {
                int width;
                int height;
                SDL_Texture* texture = muiPictureTexture(FrmId(state.tileFids[row * state.tilesPerRow + column]), &width, &height);
                if (texture == nullptr) {
                    continue;
                }

                // Whole pixels: no seams between tiles.
                float x1;
                float y1;
                float x2;
                float y2;
                toScreen(screen, static_cast<float>(column * state.tileWidth), static_cast<float>(row * state.tileHeight), &x1, &y1);
                toScreen(screen, static_cast<float>((column + 1) * state.tileWidth), static_cast<float>((row + 1) * state.tileHeight), &x2, &y2);
                MuiRect rect = { std::floor(x1), std::floor(y1), std::floor(x2) - std::floor(x1), std::floor(y2) - std::floor(y1) };
                muiDrawTexture(texture, rect);
            }
        }

    }

    void WorldmapScreen::drawFog(const MuiRect& screen)
    {
        // Explored parts: unknown ones black, known but not visited dimmed; a
        // texture of a pixel each, smoothly scaled (soft edges, no seams).
        size_t count = state.subtiles.size();
        if (count == 0) {
            return;
        }

        if (fogSource != state.subtiles) {
            fogSource = state.subtiles;
            fogPixels.resize(count * 4);
            for (size_t index = 0; index < count; index++) {
                unsigned char value = state.subtiles[index];
                fogPixels[index * 4] = 0;
                fogPixels[index * 4 + 1] = 0;
                fogPixels[index * 4 + 2] = 0;
                fogPixels[index * 4 + 3] = value == 0 ? 255 : (value == 1 ? kFogKnownAlpha : 0);
            }
            fogVersion++;
        }

        SDL_Texture* texture = muiRgbaTexture("worldmap.fog", fogPixels.data(), state.subtilesPerRow, state.subtileRows, fogVersion);
        if (texture == nullptr) {
            return;
        }
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);

        float x1;
        float y1;
        float x2;
        float y2;
        toScreen(screen, 0.0f, 0.0f, &x1, &y1);
        toScreen(screen, static_cast<float>(state.subtilesPerRow * state.subtileSize), static_cast<float>(state.subtileRows * state.subtileSize), &x2, &y2);
        muiDrawTexture(texture, { x1, y1, x2 - x1, y2 - y1 });
    }

    void WorldmapScreen::drawTowns(MuiContext& ui, const MuiRect& screen)
    {
        const MuiTheme& theme = muiTheme();
        for (const WorldmapMobileCity& city : state.cities) {
            float x;
            float y;
            toScreen(screen, city.x, city.y, &x, &y);
            float radius = std::max(city.radius * view.zoom, ui.dp(8.0f));
            if (x + radius < screen.x || x - radius > screen.right() || y + radius < screen.y || y - radius > screen.bottom() + ui.dp(30.0f)) {
                continue;
            }

            bool destination = city.area == state.destinationArea;
            bool current = city.area == state.currentArea;
            MuiColor color = destination ? kDestination : theme.accent;

            if (city.visited || current) {
                muiFillCircle(x, y, radius, color.withAlpha(current ? 40 : 22));
                muiStrokeCircle(x, y, radius, ui.dp(current ? 2.5f : 2.0f), color);
            } else {
                // Known, not visited yet: a dashed ring.
                constexpr int kDashes = 24;
                for (int dash = 0; dash < kDashes; dash += 2) {
                    float a1 = dash * 2.0f * static_cast<float>(M_PI) / kDashes;
                    float a2 = (dash + 1) * 2.0f * static_cast<float>(M_PI) / kDashes;
                    muiDrawLine(x + std::cos(a1) * radius, y + std::sin(a1) * radius, x + std::cos(a2) * radius, y + std::sin(a2) * radius, ui.dp(2.0f), color.withAlpha(210));
                }
            }

            if (!city.name.empty()) {
                std::u32string name = gameText(city.name);
                float size = ui.dp(12.0f);
                float width = muiTextWidth(name, size) + ui.dp(10.0f);
                MuiRect label = { x - width / 2.0f, y + radius + ui.dp(3.0f), width, ui.dp(18.0f) };
                muiFillRoundRect(label, ui.dp(4.0f), kLabelBackground);
                muiDrawTextAligned(name, label, size, destination ? kDestination : theme.text, MuiAlign::Center, MuiAlign::Center);
            }
        }
    }

    void WorldmapScreen::drawParty(MuiContext& ui, const MuiRect& screen)
    {
        const MuiTheme& theme = muiTheme();
        float partyX;
        float partyY;
        toScreen(screen, state.partyX, state.partyY, &partyX, &partyY);

        // Where it goes: dots to the mark (a town's ring is its mark).
        if (state.walking) {
            float destinationX;
            float destinationY;
            toScreen(screen, state.destinationX, state.destinationY, &destinationX, &destinationY);
            float dx = destinationX - partyX;
            float dy = destinationY - partyY;
            float length = std::sqrt(dx * dx + dy * dy);
            float step = ui.dp(9.0f);
            for (float t = ui.dp(12.0f); t < length; t += step) {
                muiFillCircle(partyX + dx * t / length, partyY + dy * t / length, ui.dp(2.0f), kDestination);
            }

            if (state.destinationArea == -1) {
                float arm = ui.dp(7.0f);
                muiDrawLine(destinationX - arm, destinationY - arm, destinationX + arm, destinationY + arm, ui.dp(3.0f), kDestination);
                muiDrawLine(destinationX - arm, destinationY + arm, destinationX + arm, destinationY - arm, ui.dp(3.0f), kDestination);
            }
        }

        if (state.encounter) {
            // The game's encounter icon blinks: bright and dim.
            MuiColor color = state.encounterSpecial ? kEncounterSpecial : kEncounter;
            float radius = ui.dp(state.encounterBright ? 16.0f : 12.0f);
            muiFillCircle(partyX, partyY, radius, color.withAlpha(state.encounterBright ? 90 : 40));
            muiStrokeCircle(partyX, partyY, radius, ui.dp(3.0f), color);
            muiDrawTextAligned(U"!", { partyX - radius, partyY - radius, radius * 2.0f, radius * 2.0f }, ui.dp(16.0f), color, MuiAlign::Center, MuiAlign::Center);
            return;
        }

        muiFillCircle(partyX, partyY, ui.dp(11.0f), theme.accent.withAlpha(70));
        muiFillCircle(partyX, partyY, ui.dp(6.0f), theme.accent);
        muiStrokeCircle(partyX, partyY, ui.dp(6.0f), ui.dp(1.5f), muiRgb(0x000000));
    }

    void WorldmapScreen::handleMapTap(MuiContext& ui, const MuiRect& screen)
    {
        if (state.encounter) {
            return;
        }

        float tapX = ui.pointerX();
        float tapY = ui.pointerY();
        float worldX = view.centerX + (tapX - screen.centerX()) / view.zoom;
        float worldY = view.centerY + (tapY - screen.centerY()) / view.zoom;

        // The party: enter where it stands (the game's tap on it).
        float partyX;
        float partyY;
        toScreen(screen, state.partyX, state.partyY, &partyX, &partyY);
        if (!state.walking && std::hypot(tapX - partyX, tapY - partyY) <= ui.dp(kTouchRadius)) {
            ask(MuiWorldmapActionType::Enter);
            return;
        }

        // A town: go to its center.
        for (const WorldmapMobileCity& city : state.cities) {
            float x;
            float y;
            toScreen(screen, city.x, city.y, &x, &y);
            float radius = std::max(city.radius * view.zoom, ui.dp(kTouchRadius));
            if (std::hypot(tapX - x, tapY - y) <= radius) {
                if (city.area == state.currentArea) {
                    return;
                }
                bool listed = std::find(state.destinations.begin(), state.destinations.end(), city.area) != state.destinations.end();
                if (listed) {
                    ask(MuiWorldmapActionType::TravelToArea, 0.0f, 0.0f, city.area);
                } else {
                    ask(MuiWorldmapActionType::TravelTo, city.x, city.y);
                }
                return;
            }
        }

        if (worldX >= 0.0f && worldY >= 0.0f && worldX < state.width && worldY < state.height) {
            ask(MuiWorldmapActionType::TravelTo, worldX, worldY);
        }
    }

    void WorldmapScreen::buildStatus(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        int month;
        int day;
        int year;
        gameTimeGetDate(&month, &day, &year);
        unsigned int minutes = gameTimeGetTime() / 600;
        char date[48];
        snprintf(date, sizeof(date), "%02d.%02d.%04d  %02u:%02u", day, month, year, minutes / 60 % 24, minutes % 60);
        std::u32string text = muiDecodeUtf8(date);

        float padding = ui.dp(10.0f);
        float size = ui.dp(13.0f);
        std::u32string gas = muiDecodeGameText(muiText(kTextGas, "Gas"));
        float width = std::max(muiTextWidth(text, size), state.inCar ? ui.dp(150.0f) : 0.0f) + padding * 2.0f;
        MuiRect chip = { rect.x, rect.y, width, rect.h };
        muiFillRoundRect(chip, ui.dp(theme.radius), theme.panel.withAlpha(235));
        muiStrokeRoundRect(chip, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.panelBorder);
        ui.mark("worldmap.status", chip);

        if (!state.inCar) {
            muiDrawTextAligned(text, chip.inset(padding, 0.0f), size, theme.text, MuiAlign::Start, MuiAlign::Center);
            return;
        }

        // In the car: its gas under the date.
        MuiRect line = { chip.x + padding, chip.y + ui.dp(4.0f), chip.w - padding * 2.0f, chip.h / 2.0f - ui.dp(4.0f) };
        muiDrawTextAligned(text, line, size, theme.text, MuiAlign::Start, MuiAlign::Center);
        MuiRect gasLine = { line.x, line.bottom(), line.w, chip.bottom() - line.bottom() - ui.dp(4.0f) };
        float labelSize = ui.dp(10.0f);
        float labelWidth = muiTextWidth(gas, labelSize) + ui.dp(6.0f);
        muiDrawTextAligned(gas, gasLine, labelSize, theme.textDim, MuiAlign::Start, MuiAlign::Center);
        MuiRect bar = { gasLine.x + labelWidth, gasLine.centerY() - ui.dp(3.0f), gasLine.w - labelWidth, ui.dp(6.0f) };
        muiStrokeRoundRect(bar, ui.dp(3.0f), ui.dp(1.0f), theme.panelBorder);
        muiFillRoundRect({ bar.x, bar.y, bar.w * state.fuel, bar.h }, ui.dp(3.0f), state.fuel > 0.15f ? kFuel : kEncounter);
    }

    // Sizes of the towns' list, dp.
    constexpr float kTownsInset = 8.0f;
    constexpr float kTownsHeaderHeight = 40.0f;
    constexpr float kTownsRowHeight = 44.0f;
    constexpr float kTownsGap = 5.0f;

    // In the button's place (its header the button's), as tall as its towns,
    // scrolling under its header when they don't fit.
    MuiRect WorldmapScreen::townsPanelRect(MuiContext& ui, const MuiRect& anchor) const
    {
        MuiRect safe = ui.safeRect();
        float width = std::min(ui.dp(300.0f), safe.w * 0.5f);
        float contentHeight = state.destinations.size() * ui.dp(kTownsRowHeight + kTownsGap);
        float top = anchor.y;
        float maxHeight = safe.bottom() - ui.dp(8.0f) - top;
        float height = std::min(ui.dp(kTownsHeaderHeight) + contentHeight + ui.dp(kTownsInset) * 2.0f, maxHeight);
        return { anchor.right() - width, top, width, height };
    }

    void WorldmapScreen::buildTowns(MuiContext& ui, const MuiRect& panel)
    {
        const MuiTheme& theme = muiTheme();
        float inset = ui.dp(kTownsInset);
        float headerHeight = ui.dp(kTownsHeaderHeight);
        float rowHeight = ui.dp(kTownsRowHeight);
        float gap = ui.dp(kTownsGap);
        float contentHeight = state.destinations.size() * (rowHeight + gap);

        muiFillRoundRect(panel, ui.dp(theme.radius), theme.panel.withAlpha(255));
        muiStrokeRoundRect(panel, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.accent);

        // A tap beside it only closes it (the party doesn't go there).
        if (ui.tappedOutside(panel)) {
            townsOpen = false;
        }

        MuiRect header = { panel.x + inset, panel.y + inset, panel.w - inset * 2.0f, headerHeight };
        muiDrawTextAligned(muiDecodeGameText(muiText(kTextTowns, "Towns")), { header.x + ui.dp(4.0f), header.y, header.w, header.h }, ui.dp(15.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
        MuiRect close = { header.right() - headerHeight, header.y, headerHeight, headerHeight };
        bool closePressed = false;
        if (ui.touchable("worldmap.towns.close", close, &closePressed)) {
            _gsound_red_butt_press(-1, 0);
            townsOpen = false;
        }
        muiDrawIcon(MuiIcon::Cancel, close.centerX(), close.centerY(), ui.dp(18.0f), ui.dp(2.0f), closePressed ? theme.accent : theme.text);

        MuiRect list = { panel.x + inset, header.bottom() + ui.dp(4.0f), panel.w - inset * 2.0f, panel.bottom() - header.bottom() - ui.dp(4.0f) - inset };
        float offset = ui.scroll("worldmap.towns.list", list, contentHeight);
        bool scrolling = ui.isScrolling("worldmap.towns.list");

        muiPushClip(list);
        float y = list.y - offset;
        for (int area : state.destinations) {
            MuiRect row = { list.x, y, list.w, rowHeight };
            y += rowHeight + gap;
            if (row.bottom() < list.y || row.y > list.bottom()) {
                continue;
            }

            bool here = area == state.currentArea;
            bool destination = area == state.destinationArea;
            bool visited = false;
            for (const WorldmapMobileCity& city : state.cities) {
                if (city.area == area) {
                    visited = city.visited;
                }
            }

            bool pressed = false;
            if (ui.touchable("worldmap.towns.list." + std::to_string(area), row, &pressed) && !scrolling && !here) {
                _gsound_red_butt_press(-1, 0);
                townsOpen = false;
                ask(MuiWorldmapActionType::TravelToArea, 0.0f, 0.0f, area);
            }

            MuiColor border = destination ? kDestination : theme.buttonBorder;
            muiFillRoundRect(row, ui.dp(theme.radius - 2.0f), pressed ? theme.buttonPressed : theme.button);
            muiStrokeRoundRect(row, ui.dp(theme.radius - 2.0f), ui.dp(1.0f), border);

            std::u32string tag;
            if (here) {
                tag = muiDecodeGameText(muiText(kTextHere, "you're here"));
            } else if (destination) {
                tag = muiDecodeGameText(muiText(kTextDestination, "destination"));
            } else if (visited) {
                tag = muiDecodeGameText(muiText(kTextVisited, "visited"));
            }

            MuiRect text = row.inset(ui.dp(10.0f), 0.0f);
            float tagSize = ui.dp(11.0f);
            float tagWidth = tag.empty() ? 0.0f : muiTextWidth(tag, tagSize) + ui.dp(8.0f);
            std::u32string name = areaName(area);
            MuiRect nameRect = { text.x, text.y, text.w - tagWidth, text.h };
            muiDrawTextAligned(name, nameRect, muiFitTextSize(name, nameRect.w, ui.dp(14.0f), ui.dp(10.0f)), destination ? kDestination : theme.text, MuiAlign::Start, MuiAlign::Center);
            if (!tag.empty()) {
                muiDrawTextAligned(tag, text, tagSize, destination ? kDestination : theme.textDim, MuiAlign::End, MuiAlign::Center);
            }
        }
        muiPopClip();
    }

    void WorldmapScreen::buildEnter(MuiContext& ui, const MuiRect& safe)
    {
        const MuiTheme& theme = muiTheme();
        if (state.walking || state.encounter) {
            return;
        }

        // Where it stands: a town, or the wasteland (its encounter map).
        std::u32string label;
        if (state.currentArea >= 0) {
            std::u32string format = muiDecodeGameText(muiText(kTextEnter, "Enter %s"));
            std::u32string name = areaName(state.currentArea);
            size_t at = format.find(U"%s");
            label = at != std::u32string::npos ? format.substr(0, at) + name + format.substr(at + 2) : format + U" " + name;
        } else {
            label = muiDecodeGameText(muiText(kTextEnterWasteland, "Enter the wasteland"));
        }

        float height = ui.dp(48.0f);
        float width = std::clamp(muiTextWidth(label, ui.dp(theme.buttonTextSize)) + ui.dp(48.0f), ui.dp(200.0f), safe.w * 0.6f);
        MuiRect button = { safe.centerX() - width / 2.0f, safe.bottom() - height - ui.dp(12.0f), width, height };
        if (ui.button("worldmap.enter", button, label, MuiButtonStyle::Primary)) {
            ask(MuiWorldmapActionType::Enter);
        }
    }

    void WorldmapScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        wmMobileGetState(&state);

        MuiRect screen = ui.screenRect();
        muiFillRect(screen, kFogUnknown);
        if (state.width <= 0 || state.height <= 0) {
            return;
        }

        setupView(screen, ui);

        // A new destination: the camera goes with the party again.
        if (state.walking && !wasWalking) {
            following = true;
        }
        wasWalking = state.walking;
        if (following && settings.enhancements.worldmap_follow_party && state.walking) {
            view.centerX += (state.partyX - view.centerX) * 0.2f;
            view.centerY += (state.partyY - view.centerY) * 0.2f;
        }
        keepInside(screen);

        // Towns under the fog, as the game draws them.
        drawMap(ui, screen);
        drawTowns(ui, screen);
        drawFog(screen);
        drawParty(ui, screen);

        // As the touch HUD over the map: the game menu (the game's options
        // mode, `muiWorldmapMenuRun`) hides what floats over the world.
        if ((GameMode::getCurrentGameMode() & GameMode::kOptions) != 0) {
            return;
        }

        // What floats over the map takes its touches first.
        MuiRect safe = ui.safeRect();
        float margin = ui.dp(12.0f);
        float size = ui.dp(48.0f);

        MuiRect menu = { safe.x + margin, safe.y + margin, size, size };
        bool menuPressed = false;
        if (ui.touchable("worldmap.menu", menu, &menuPressed)) {
            _gsound_red_butt_press(-1, 0);
            townsOpen = false;
            ask(MuiWorldmapActionType::Menu);
        }
        muiFillRoundRect(menu, ui.dp(theme.radius), menuPressed ? theme.buttonPressed : theme.panel.withAlpha(235));
        muiStrokeRoundRect(menu, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.panelBorder);
        muiDrawIcon(MuiIcon::Menu, menu.centerX(), menu.centerY(), ui.dp(24.0f), ui.dp(2.0f), theme.accent);

        buildStatus(ui, { menu.right() + ui.dp(8.0f), menu.y, 0.0f, size });

        std::u32string townsLabel = muiDecodeGameText(muiText(kTextTowns, "Towns"));
        float townsWidth = muiTextWidth(townsLabel, ui.dp(15.0f)) + ui.dp(56.0f);
        MuiRect towns = { safe.right() - margin - townsWidth, safe.y + margin, townsWidth, size };
        // The open list takes the button's place.
        if (!townsOpen) {
            bool townsPressed = false;
            if (ui.touchable("worldmap.towns", towns, &townsPressed)) {
                _gsound_red_butt_press(-1, 0);
                townsOpen = true;
            }
            muiFillRoundRect(towns, ui.dp(theme.radius), townsPressed ? theme.buttonPressed : theme.panel.withAlpha(235));
            muiStrokeRoundRect(towns, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.panelBorder);
            muiDrawIcon(MuiIcon::Map, towns.x + ui.dp(22.0f), towns.centerY(), ui.dp(20.0f), ui.dp(2.0f), theme.accent);
            muiDrawTextAligned(townsLabel, { towns.x + ui.dp(40.0f), towns.y, towns.w - ui.dp(46.0f), towns.h }, ui.dp(15.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
        }

        // "Enter" stays under the open list, which covers it where they meet
        // (a touch there is the list's).
        if (townsOpen) {
            MuiRect panel = townsPanelRect(ui, towns);
            bool interactive = ui.isInteractive;
            if (panel.contains(ui.pointerX(), ui.pointerY())) {
                ui.isInteractive = false;
            }
            buildEnter(ui, safe);
            ui.isInteractive = interactive;
            buildTowns(ui, panel);
        } else {
            buildEnter(ui, safe);

            // The map: a finger moves it (the camera stops following), two
            // zoom it, a tap goes there.
            bool tapped = false;
            if (ui.panZoom("worldmap.map", screen, &view, &tapped)) {
                if (ui.pointerDown() && !tapped) {
                    following = false;
                }
                keepInside(screen);
            }
            if (tapped) {
                handleMapTap(ui, screen);
            }
        }
    }

    WorldmapScreen* gWorldmapScreen = nullptr;

    // The town's picture from the game with its entrances; the town's name at
    // the top left, Back at the right edge.
    class TownMapScreen : public MuiScreen {
    public:
        explicit TownMapScreen(const MuiTownMapView& view)
            : view(view)
        {
            modal = true;
        }

        bool coversScreen() override { return true; }
        bool hidesHud() override { return true; }
        void build(MuiContext& ui) override;
        void back() override { finished = true; }

        int result = -1;

    private:
        MuiTownMapView view;
    };

    void TownMapScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        muiFillRect(ui.screenRect(), kFogUnknown);

        MuiRect content;
        MuiRect rail;
        muiDialogLayout(ui, &content, &rail);
        if (muiGameScreenTabs(ui, rail, "townmap", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back) {
            back();
        }

        int width;
        int height;
        SDL_Texture* texture = muiPictureTexture(FrmId(view.pictureFid), &width, &height);
        MuiRect picture = content;
        float scale = 1.0f;
        if (texture != nullptr && width > 0 && height > 0) {
            picture = muiFitRect(content, static_cast<float>(width), static_cast<float>(height));
            scale = picture.w / width;
            muiDrawTexture(texture, picture);
        }

        // The town's name, framed, over the picture's top left.
        std::u32string name = gameText(view.name);
        float nameSize = ui.dp(16.0f);
        MuiRect title = { content.x, content.y, muiTextWidth(name, nameSize) + ui.dp(28.0f), ui.dp(44.0f) };
        muiFillRoundRect(title, ui.dp(theme.radius), theme.panel.withAlpha(235));
        muiStrokeRoundRect(title, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.accent);
        muiDrawTextAligned(name, title, nameSize, theme.text, MuiAlign::Center, MuiAlign::Center);

        // Entrances: a mark with its name under it; both enter.
        for (const MuiTownMapEntrance& entrance : view.entrances) {
            float x = picture.x + entrance.x * scale;
            float y = picture.y + entrance.y * scale;
            std::u32string label = gameText(entrance.name);
            float labelSize = ui.dp(13.0f);
            float labelWidth = muiTextWidth(label, labelSize) + ui.dp(16.0f);
            float markRadius = ui.dp(9.0f);
            MuiRect labelRect = { x - labelWidth / 2.0f, y + markRadius + ui.dp(4.0f), labelWidth, ui.dp(28.0f) };
            MuiRect target = { std::min(labelRect.x, x - ui.dp(kTouchRadius)), y - ui.dp(kTouchRadius), std::max(labelRect.w, ui.dp(kTouchRadius) * 2.0f), labelRect.bottom() - (y - ui.dp(kTouchRadius)) };

            bool pressed = false;
            if (ui.touchable("townmap.entrance." + std::to_string(entrance.index), target, &pressed)) {
                _gsound_red_butt_press(-1, 0);
                result = entrance.index;
                finished = true;
            }

            muiFillCircle(x, y, markRadius, pressed ? theme.accent : theme.panel);
            muiStrokeCircle(x, y, markRadius, ui.dp(2.5f), theme.accent);
            if (!label.empty()) {
                muiFillRoundRect(labelRect, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.panel.withAlpha(235));
                muiStrokeRoundRect(labelRect, ui.dp(6.0f), ui.dp(1.0f), theme.buttonBorder);
                muiDrawTextAligned(label, labelRect, labelSize, theme.text, MuiAlign::Center, MuiAlign::Center);
            }
        }
    }

} // namespace

void muiWorldmapShow()
{
    if (gWorldmapScreen == nullptr) {
        gWorldmapScreen = new WorldmapScreen();
        muiPush(gWorldmapScreen);
    }
}

void muiWorldmapHide()
{
    if (gWorldmapScreen != nullptr) {
        muiRemove(gWorldmapScreen);
        delete gWorldmapScreen;
        gWorldmapScreen = nullptr;
    }
}

void muiWorldmapBack()
{
    if (gWorldmapScreen != nullptr) {
        gWorldmapScreen->back();
    }
}

MuiWorldmapAction muiWorldmapTakeAction()
{
    MuiWorldmapAction action;
    if (gWorldmapScreen != nullptr) {
        action = gWorldmapScreen->pending;
        gWorldmapScreen->pending = MuiWorldmapAction();
    }
    return action;
}

void muiWorldmapMenuRun()
{
    // The game's options, as `showOptions`: its game mode is on meanwhile.
    ScopedGameMode gm(GameMode::kOptions);

    std::u32string place = gWorldmapScreen != nullptr ? gWorldmapScreen->partyPlace() : std::u32string();
    muiGameMenuRun(false, &place);
}

int muiTownMapRun(const MuiTownMapView& view)
{
    TownMapScreen screen(view);
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        } else if (keyCode >= KEY_1 && keyCode <= KEY_9) {
            // The game's number keys: its entrances shown.
            for (const MuiTownMapEntrance& entrance : view.entrances) {
                if (entrance.index == keyCode - KEY_1) {
                    screen.result = entrance.index;
                    screen.finished = true;
                }
            }
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
    return screen.result;
}

} // namespace fallout
