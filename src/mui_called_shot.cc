#include "mui.h"
#include "mui_icons.h"
#include "mui_screens.h"

#include <algorithm>
#include <string>

#include "art.h"
#include "combat.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_sound.h"
#include "hud_layout.h"
#include "input.h"
#include "kb.h"
#include "object.h"
#include "svga.h"
#include "touch_hud.h"

namespace fallout {

namespace {

    const MuiColor kPanel = muiRgb(0x0B120D, 245);
    const MuiColor kLow = muiRgb(0xFF6E5E);
    const MuiColor kMiddle = muiRgb(0xFFB000);

    // Called shot: the target's picture between its hit locations (the
    // game's two columns), each with its name and hit chance. A panel over
    // the map where the HUD leaves room; a tap outside or back cancels.
    class CalledShotScreen : public MuiScreen {
    public:
        Object* critter = nullptr;
        CalledShotTarget targets[kCalledShotTargetCount];
        int chosen = -1;
        bool cancelled = false;

        CalledShotScreen()
        {
            modal = true;
        }

        void build(MuiContext& ui) override;
        void back() override { cancelled = true; }

    private:
        MuiRect freeArea(MuiContext& ui);
        void buildTarget(MuiContext& ui, int index, const MuiRect& rect, bool rightColumn);
    };

    CalledShotScreen gCalledShotScreen;

    // Room the HUD leaves: right of the left edge buttons, under the top
    // row, left of the combat and weapon groups.
    MuiRect CalledShotScreen::freeArea(MuiContext& ui)
    {
        MuiRect safe = ui.safeRect().inset(ui.dp(10.0f));
        float gap = ui.dp(12.0f);
        float left = safe.x;
        float top = safe.y;
        float right = safe.right();

        MuiRect rect;
        for (HudElementId id : { HudElementId::Skills, HudElementId::Highlight, HudElementId::Sneak, HudElementId::Log }) {
            if (touchHudGetElementRect(id, &rect) && rect.w > 0.0f) {
                left = std::max(left, rect.right() + gap);
            }
        }

        for (HudElementId id : { HudElementId::Menu, HudElementId::QuickSave, HudElementId::Inventory, HudElementId::Map }) {
            if (touchHudGetElementRect(id, &rect) && rect.w > 0.0f) {
                top = std::max(top, rect.bottom() + gap);
            }
        }

        for (HudElementId id : { HudElementId::EndTurn, HudElementId::EndCombat, HudElementId::Status, HudElementId::Modes, HudElementId::Weapon }) {
            if (touchHudGetElementRect(id, &rect) && rect.w > 0.0f) {
                right = std::min(right, rect.x - gap);
            }
        }

        if (right - left < ui.dp(360.0f)) {
            return safe;
        }

        return { left, top, right - left, safe.bottom() - top };
    }

    void CalledShotScreen::buildTarget(MuiContext& ui, int index, const MuiRect& rect, bool rightColumn)
    {
        const MuiTheme& theme = muiTheme();
        const CalledShotTarget& target = targets[index];

        bool pressed;
        if (ui.touchable("calledshot." + std::to_string(index), rect, &pressed)) {
            chosen = index;
        }

        muiFillRoundRect(rect, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);

        // Chance at the outer side, name next to the picture.
        // No chance: dashes, like the game's window.
        std::string chanceText = target.chance >= 0 ? std::to_string(target.chance) + "%" : "--";
        std::u32string chance(chanceText.begin(), chanceText.end());
        MuiColor chanceColor = target.chance >= 50 ? theme.accent : (target.chance >= 25 ? kMiddle : kLow);
        float chanceSize = ui.dp(18.0f);
        float chanceWidth = muiTextWidth(U"100%", chanceSize);
        float padding = ui.dp(10.0f);

        MuiRect chanceRect = rightColumn
            ? MuiRect { rect.right() - padding - chanceWidth, rect.y, chanceWidth, rect.h }
            : MuiRect { rect.x + padding, rect.y, chanceWidth, rect.h };
        muiDrawTextAligned(chance, chanceRect, chanceSize, chanceColor, rightColumn ? MuiAlign::End : MuiAlign::Start, MuiAlign::Center);

        std::u32string name = muiDecodeGameText(target.name);
        MuiRect nameRect = rightColumn
            ? MuiRect { rect.x + padding, rect.y, chanceRect.x - rect.x - padding * 2.0f, rect.h }
            : MuiRect { chanceRect.right() + padding, rect.y, rect.right() - chanceRect.right() - padding * 2.0f, rect.h };
        float nameSize = muiFitTextSize(name, nameRect.w, ui.dp(14.0f), ui.dp(10.0f));
        muiDrawTextAligned(name, nameRect, nameSize, theme.text, rightColumn ? MuiAlign::Start : MuiAlign::End, MuiAlign::Center);
    }

    void CalledShotScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        MuiRect area = freeArea(ui);

        float width = std::min(area.w, ui.dp(560.0f));
        float height = std::min(area.h, ui.dp(300.0f));
        MuiRect panel = { area.centerX() - width / 2.0f, area.centerY() - height / 2.0f, width, height };

        // Outside: cancel (the HUD stays visible, not touchable meanwhile).
        if (ui.tappedOutside(panel)) {
            cancelled = true;
        }

        muiFillRoundRect(panel, ui.dp(theme.radius), kPanel);
        muiStrokeRoundRect(panel, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.accent);
        ui.region(panel);

        MuiRect inner = panel.inset(ui.dp(10.0f));

        // Header: the target, close at the right.
        float headerHeight = ui.dp(32.0f);
        MuiRect close = { inner.right() - headerHeight, inner.y, headerHeight, headerHeight };
        bool closePressed;
        if (ui.touchable("calledshot.cancel", close, &closePressed)) {
            _gsound_red_butt_press(-1, 0);
            cancelled = true;
        }
        muiFillRoundRect(close, ui.dp(6.0f), closePressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(close, ui.dp(6.0f), ui.dp(1.0f), theme.buttonBorder);
        muiDrawIcon(MuiIcon::Cancel, close.centerX(), close.centerY(), close.h * 0.45f, ui.dp(2.0f), theme.accent);

        std::u32string name = muiDecodeGameText(objectGetName(critter));
        MuiRect nameRect = { inner.x, inner.y, close.x - inner.x - ui.dp(8.0f), headerHeight };
        muiDrawTextAligned(name, nameRect, muiFitTextSize(name, nameRect.w, ui.dp(16.0f), ui.dp(11.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);

        // Columns: locations, picture, locations.
        MuiRect body = { inner.x, inner.y + headerHeight + ui.dp(8.0f), inner.w, inner.bottom() - inner.y - headerHeight - ui.dp(8.0f) };
        float gap = ui.dp(8.0f);
        float pictureWidth = std::min(body.w * 0.3f, body.h * 170.0f / 225.0f);
        float columnWidth = (body.w - pictureWidth - gap * 2.0f) / 2.0f;
        MuiRect leftColumn = { body.x, body.y, columnWidth, body.h };
        MuiRect picture = { leftColumn.right() + gap, body.y, pictureWidth, body.h };
        MuiRect rightColumn = { picture.right() + gap, body.y, columnWidth, body.h };

        int width2;
        int height2;
        const FrmId frmId(critter, ANIM_CALLED_SHOT_PIC, WeaponAnimation::None, ROTATION_NE);
        SDL_Texture* texture = muiArtTexture(frmId, 0, &width2, &height2);
        if (texture != nullptr) {
            muiDrawTexture(texture, muiFitRect(picture, static_cast<float>(width2), static_cast<float>(height2)));
        }

        float rowGap = ui.dp(6.0f);
        float rowHeight = std::min((body.h - rowGap * 3.0f) / 4.0f, ui.dp(56.0f));
        float top = body.y + (body.h - rowHeight * 4.0f - rowGap * 3.0f) / 2.0f;
        for (int row = 0; row < 4; row++) {
            float y = top + row * (rowHeight + rowGap);
            buildTarget(ui, row, { leftColumn.x, y, leftColumn.w, rowHeight }, false);
            buildTarget(ui, row + 4, { rightColumn.x, y, rightColumn.w, rowHeight }, true);
        }
    }

} // namespace

int muiSelectCalledShot(Object* critter, int hitMode, HitLocation* location)
{
    CalledShotScreen& screen = gCalledShotScreen;
    screen.critter = critter;
    screen.chosen = -1;
    screen.cancelled = false;
    calledShotGetTargets(critter, static_cast<HitMode>(hitMode), screen.targets);
    muiPush(&screen);

    while (screen.chosen == -1 && !screen.cancelled) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        }

        devAutotestTick();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.cancelled = true;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);

    if (screen.chosen == -1) {
        return -1;
    }

    *location = screen.targets[screen.chosen].location;
    return 0;
}

} // namespace fallout
