#include "mui.h"

#include <algorithm>
#include <string>
#include <vector>

#include "art.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "input.h"
#include "kb.h"
#include "svga.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x000000);
    const MuiColor kSubtitleBand = muiRgb(0x000000, 170);
    const MuiColor kSubtitleText = muiRgb(0xFFFFFF);

    // The death screen and the ending slides: the game's picture (or the
    // frame the game composed) fit to the screen on black, in the game's
    // palette and its fades; the subtitle in the UI's font over its bottom.
    // A tap is taken by the game's loop as its key (ends the scene).
    class SceneScreen : public MuiScreen {
    public:
        SceneScreen() { modal = true; }

        bool coversScreen() override { return true; }
        void build(MuiContext& ui) override;
        // As a tap (the game's loop takes it as its key).
        void back() override { tapped = true; }

        InterfaceFrameId picture = InterfaceFrameId::Invalid;
        std::vector<unsigned char> frame;
        int frameWidth = 0;
        int frameHeight = 0;
        unsigned int frameVersion = 0;
        std::u32string subtitle;
        bool tapped = false;
    };

    void SceneScreen::build(MuiContext& ui)
    {
        MuiRect screen = ui.screenRect();
        muiFillRect(screen, kBackground);

        MuiRect shown = screen;
        if (picture != InterfaceFrameId::Invalid) {
            int width;
            int height;
            SDL_Texture* texture = muiPictureTexture(FrmId(picture), &width, &height);
            if (texture != nullptr) {
                shown = muiFitRect(screen, static_cast<float>(width), static_cast<float>(height));
                muiDrawTexture(texture, shown);
            }
        } else if (!frame.empty()) {
            // Screen sized: its own fitting is the game's.
            SDL_Texture* texture = muiIndexedTexture("scene.frame", frame.data(), frameWidth, frameHeight, frameVersion);
            if (texture != nullptr) {
                SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
                muiDrawTexture(texture, screen);
            }
        }

        if (!subtitle.empty()) {
            MuiRect safe = ui.safeRect();
            float size = ui.dp(15.0f);
            float lineHeight = muiLineHeight(size);
            float width = std::min(safe.w - ui.dp(48.0f), ui.dp(640.0f));
            std::vector<std::u32string> lines = muiWrapText(subtitle, width, size);
            float padding = ui.dp(10.0f);
            float height = lines.size() * lineHeight + padding * 2.0f;
            float bottom = std::min(shown.bottom(), safe.bottom()) - ui.dp(12.0f);
            MuiRect band = { safe.centerX() - width / 2.0f - padding, bottom - height, width + padding * 2.0f, height };
            muiFillRoundRect(band, ui.dp(8.0f), kSubtitleBand);
            float y = band.y + padding;
            for (const std::u32string& line : lines) {
                muiDrawTextAligned(line, { band.x, y, band.w, lineHeight }, size, kSubtitleText, MuiAlign::Center, MuiAlign::Center);
                y += lineHeight;
            }
        }

        if (ui.touchable("scene.screen", screen)) {
            tapped = true;
        }
    }

    SceneScreen* gSceneScreen = nullptr;

    // Black while a game loads.
    class CurtainScreen : public MuiScreen {
    public:
        CurtainScreen() { modal = true; }

        bool coversScreen() override { return true; }
        bool hidesHud() override { return true; }
        void build(MuiContext& ui) override { muiFillRect(ui.screenRect(), kBackground); }
    };

    CurtainScreen* gCurtainScreen = nullptr;

    // Credits (or quotes) rolling up over black in the UI's font, the game's
    // colors; a tap or a key stops them.
    class CreditsScreen : public MuiScreen {
    public:
        explicit CreditsScreen(const std::vector<MuiCreditsLine>& lines)
            : lines(lines)
        {
            modal = true;
        }

        bool coversScreen() override { return true; }
        void build(MuiContext& ui) override;
        void back() override { finished = true; }

    private:
        std::vector<MuiCreditsLine> lines;
        unsigned int start = 0;
    };

    void CreditsScreen::build(MuiContext& ui)
    {
        MuiRect screen = ui.screenRect();
        muiFillRect(screen, kBackground);

        // The game's pace: a line every 12 steps of 38 ms.
        float lineHeight = ui.dp(26.0f);
        float speed = lineHeight / 456.0f;
        if (start == 0) {
            start = ui.now;
        }
        float offset = (ui.now - start) * speed;

        MuiRect safe = ui.safeRect();
        float y = screen.bottom() - offset;
        for (const MuiCreditsLine& line : lines) {
            if (y > -lineHeight && y < screen.bottom()) {
                float size = line.title ? ui.dp(17.0f) : ui.dp(14.0f);
                std::u32string text = muiDecodeGameText(line.text.c_str());
                size = muiFitTextSize(text, safe.w - ui.dp(32.0f), size, ui.dp(9.0f));
                muiDrawTextAligned(text, { safe.x, y, safe.w, lineHeight }, size, line.color, MuiAlign::Center, MuiAlign::Center);
            }
            y += lineHeight;
        }

        // Rolled off the top.
        if (y < screen.y) {
            finished = true;
        }

        if (ui.touchable("credits.screen", screen)) {
            finished = true;
        }
    }

} // namespace

void muiSceneShow()
{
    if (gSceneScreen == nullptr) {
        gSceneScreen = new SceneScreen();
        muiPush(gSceneScreen);
    }
}

void muiSceneHide()
{
    if (gSceneScreen != nullptr) {
        muiRemove(gSceneScreen);
        delete gSceneScreen;
        gSceneScreen = nullptr;
    }
}

void muiSceneSetPicture(InterfaceFrameId picture)
{
    if (gSceneScreen != nullptr) {
        gSceneScreen->picture = picture;
        gSceneScreen->frame.clear();
    }
}

void muiSceneSetFrame(const unsigned char* pixels, int width, int height)
{
    if (gSceneScreen != nullptr && pixels != nullptr && width > 0 && height > 0) {
        gSceneScreen->picture = InterfaceFrameId::Invalid;
        gSceneScreen->frame.assign(pixels, pixels + static_cast<size_t>(width) * height);
        gSceneScreen->frameWidth = width;
        gSceneScreen->frameHeight = height;
        gSceneScreen->frameVersion++;
    }
}

void muiSceneSetSubtitle(const char* text)
{
    if (gSceneScreen != nullptr) {
        gSceneScreen->subtitle = text != nullptr ? muiDecodeGameText(text) : std::u32string();
    }
}

bool muiSceneTakeTap()
{
    if (gSceneScreen == nullptr || !gSceneScreen->tapped) {
        return false;
    }
    gSceneScreen->tapped = false;
    return true;
}

void muiCurtainShow()
{
    if (gCurtainScreen == nullptr) {
        gCurtainScreen = new CurtainScreen();
        muiPush(gCurtainScreen);
    }
}

void muiCurtainHide()
{
    if (gCurtainScreen != nullptr) {
        muiRemove(gCurtainScreen);
        delete gCurtainScreen;
        gCurtainScreen = nullptr;
    }
}

void muiCreditsRun(const std::vector<MuiCreditsLine>& lines)
{
    CreditsScreen screen(lines);
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if ((keyCode != -1 && keyCode != -2) || _game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
}

} // namespace fallout
