#include "mui.h"
#include "mui_screens.h"

#include <ctype.h>
#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "art.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "svga.h"

namespace fallout {

namespace {

    // The game's panel layout (art pixels).
    constexpr int kButtonX = 13;
    constexpr int kButtonY = 40;
    constexpr int kButtonStep = 60;
    constexpr int kGaugeX = 121;
    constexpr int kGaugeY = 41;
    // The gauge art: 13 needle positions one under another, 12 steps for
    // the whole way.
    constexpr int kGaugeFrames = 13;
    constexpr float kGaugeSteps = 12.0f;
    // A finger's target reaches past a button over its level's name.
    constexpr int kTargetWidth = 110;
    // The game pauses after the gauge stops.
    constexpr unsigned int kArrivalPause = 200;

    // Texts in `game\ce.msg`.
    constexpr int kTextTitle = 313;
    constexpr int kTextCurrentLevel = 314;

    // The game's elevator panel (each elevator its own, with its levels'
    // names) in a card of the UI over the dimmed map, the level the dude is at
    // over it: tapping a level's button moves the gauge's needle there (with
    // the lift's sound), then the game takes the dude there. Back at the right
    // edge or a tap outside the card stays.
    class ElevatorScreen : public MuiScreen {
    public:
        explicit ElevatorScreen(const MuiElevatorView& view);

        bool hidesHud() override { return true; }
        void build(MuiContext& ui) override;
        void back() override;
        void key(int keyCode) override;

        int result = -1;

    private:
        MuiElevatorView view;
        // Chosen level, -1 - none yet.
        int target = -1;
        bool travelling = false;
        unsigned int travelStart = 0;
        unsigned int arrived = 0;

        void choose(int level);
        float gaugePosition(unsigned int now);
    };

    ElevatorScreen::ElevatorScreen(const MuiElevatorView& view)
        : view(view)
    {
        modal = true;
    }

    void ElevatorScreen::back()
    {
        if (target == -1) {
            finished = true;
        }
    }

    void ElevatorScreen::key(int keyCode)
    {
        for (int level = 0; level < view.levels && level < 4; level++) {
            if (view.keys[level] != '\0' && toupper(view.keys[level]) == toupper(keyCode & 0xFF)) {
                choose(level);
                return;
            }
        }
    }

    void ElevatorScreen::choose(int level)
    {
        if (target != -1 || level < 0 || level >= view.levels) {
            return;
        }

        target = level;
        if (level == view.level) {
            // Already there: the game goes on without the gauge.
            result = level;
            finished = true;
            return;
        }

        travelling = true;
        travelStart = 0;
        if (view.onTravel) {
            view.onTravel(view.level, level);
        }
    }

    float ElevatorScreen::gaugePosition(unsigned int now)
    {
        float unitsPerLevel = kGaugeSteps / std::max(view.levels - 1, 1);
        float from = view.level * unitsPerLevel;
        if (!travelling) {
            return from;
        }

        if (travelStart == 0) {
            travelStart = now;
        }

        float to = target * unitsPerLevel;
        float moved = (now - travelStart) / std::max(view.gaugeStepTime, 1.0f);
        if (moved >= std::abs(to - from)) {
            if (arrived == 0) {
                arrived = now;
            } else if (now - arrived >= kArrivalPause) {
                result = target;
                finished = true;
            }
            return to;
        }
        return to > from ? from + moved : from - moved;
    }

    void ElevatorScreen::build(MuiContext& ui)
    {
        ui.dim();

        MuiRect content;
        MuiRect rail;
        muiDialogLayout(ui, &content, &rail);
        if (target == -1 && muiGameScreenTabs(ui, rail, "elevator", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back) {
            back();
        }

        int width;
        int height;
        SDL_Texture* background = muiArtTexture(FrmId(view.background), 0, &width, &height);
        if (background == nullptr || width <= 0 || height <= 0) {
            // Nothing to show: stay, as the game does without its art.
            finished = true;
            return;
        }

        // The card: a title line, the panel under it.
        const MuiTheme& theme = muiTheme();
        float padding = ui.dp(8.0f);
        float titleHeight = ui.dp(26.0f);
        float scale = std::min((content.h * 0.96f - titleHeight - padding * 3.0f) / height, content.w * 0.9f / width);
        MuiRect card = { 0.0f, 0.0f, width * scale + padding * 2.0f, height * scale + titleHeight + padding * 3.0f };
        card.x = content.centerX() - card.w / 2.0f;
        card.y = content.centerY() - card.h / 2.0f;

        if (target == -1 && ui.tappedOutside(card)) {
            back();
        }

        muiFillRoundRect(card, ui.dp(theme.radius), theme.panel);
        muiStrokeRoundRect(card, ui.dp(theme.radius), ui.dp(1.5f), theme.accent);

        MuiRect title = { card.x + padding * 1.5f, card.y + padding, card.w - padding * 3.0f, titleHeight };
        muiDrawTextAligned(muiDecodeGameText(muiText(kTextTitle, "Elevator")), title, ui.dp(14.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
        if (view.level >= 0 && view.level < 4 && view.keys[view.level] != '\0') {
            char name[2] = { view.keys[view.level], '\0' };
            char text[64];
            snprintf(text, sizeof(text), muiText(kTextCurrentLevel, "you're on level %s"), name);
            std::u32string current = muiDecodeGameText(text);
            muiDrawTextAligned(current, title, muiFitTextSize(current, title.w * 0.6f, ui.dp(12.0f), ui.dp(9.0f)), theme.textDim, MuiAlign::End, MuiAlign::Center);
        }

        MuiRect panel = { card.x + padding, title.bottom() + padding, width * scale, height * scale };
        auto art = [&](float x, float y, float w, float h) {
            return MuiRect { panel.x + x * scale, panel.y + y * scale, w * scale, h * scale };
        };

        muiDrawTexture(background, panel);

        if (view.panel != InterfaceFrameId::Invalid) {
            int panelWidth;
            int panelHeight;
            SDL_Texture* texture = muiArtTexture(FrmId(view.panel), 0, &panelWidth, &panelHeight);
            if (texture != nullptr) {
                muiDrawTexture(texture, art(0.0f, static_cast<float>(height - panelHeight), static_cast<float>(panelWidth), static_cast<float>(panelHeight)));
            }
        }

        int gaugeWidth;
        int gaugeHeight;
        SDL_Texture* gauge = muiArtTexture(FrmId(InterfaceFrameId::MapElevatorGaj000), 0, &gaugeWidth, &gaugeHeight);
        if (gauge != nullptr) {
            int frameHeight = gaugeHeight / kGaugeFrames;
            int frame = std::clamp(static_cast<int>(gaugePosition(ui.now)), 0, kGaugeFrames - 1);
            muiDrawTexturePart(gauge, { 0, frame * frameHeight, gaugeWidth, frameHeight },
                art(static_cast<float>(kGaugeX), static_cast<float>(kGaugeY), static_cast<float>(gaugeWidth), static_cast<float>(frameHeight)));
        }

        int buttonWidth;
        int buttonHeight;
        SDL_Texture* pressedButton = muiArtTexture(FrmId(InterfaceFrameId::MapElevatorButtonIn), 0, &buttonWidth, &buttonHeight);
        for (int level = 0; level < view.levels; level++) {
            float y = static_cast<float>(kButtonY + kButtonStep * level);
            MuiRect button = art(static_cast<float>(kButtonX), y, static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
            MuiRect target = art(0.0f, y - (kButtonStep - buttonHeight) / 2.0f, static_cast<float>(std::min(kTargetWidth, width)), static_cast<float>(kButtonStep));

            bool pressed = false;
            if (ui.touchable("elevator.level." + std::to_string(level), target, &pressed) && this->target == -1) {
                _gsound_red_butt_press(-1, 0);
                choose(level);
            }
            if (pressedButton != nullptr && (pressed || this->target == level)) {
                muiDrawTexture(pressedButton, button);
            }
        }
    }

} // namespace

int muiElevatorRun(const MuiElevatorView& view)
{
    ElevatorScreen screen(view);
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        } else if (keyCode > 0) {
            screen.key(keyCode);
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
