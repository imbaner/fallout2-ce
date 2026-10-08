#include "mui_screens.h"

#include <algorithm>
#include <string>
#include <vector>

#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "input.h"
#include "mouse.h"
#include "movie.h"
#include "svga.h"

namespace fallout {

void muiDrawMovie(MuiContext& ui, const MuiRect& rect)
{
    muiFillRect(rect, muiRgb(0x000000));

    int width;
    int height;
    SDL_Texture* frame = movieGetFrameTexture(&width, &height);

    // Room for two subtitle lines under the picture when it fits.
    float size = std::max(ui.dp(14.0f), rect.h * 0.045f);
    float lineHeight = muiLineHeight(size);
    const char* subtitle = movieGetSubtitle();

    MuiRect picture = rect;
    if (frame != nullptr) {
        picture = muiFitRect(rect, static_cast<float>(width), static_cast<float>(height));
        // Smooth: the movie is scaled by any factor.
        SDL_SetTextureScaleMode(frame, SDL_ScaleModeLinear);
        muiDrawTexture(frame, picture);
    }

    if (subtitle == nullptr) {
        return;
    }

    std::vector<std::u32string> lines = muiWrapText(muiDecodeGameText(subtitle), rect.w - ui.dp(24.0f), size);
    float textHeight = lines.size() * lineHeight;
    float y = rect.bottom() - textHeight - ui.dp(10.0f);
    MuiRect background = { rect.x + ui.dp(8.0f), y - ui.dp(4.0f), rect.w - ui.dp(16.0f), textHeight + ui.dp(8.0f) };
    muiFillRoundRect(background, ui.dp(6.0f), muiRgb(0x000000, 150));
    for (const std::u32string& line : lines) {
        MuiRect lineRect = { rect.x, y, rect.w, lineHeight };
        muiDrawTextAligned(line, { lineRect.x + ui.dp(1.0f), lineRect.y + ui.dp(1.0f), lineRect.w, lineRect.h }, size, muiRgb(0x000000), MuiAlign::Center, MuiAlign::Start);
        muiDrawTextAligned(line, lineRect, size, muiRgb(0xFFFFFF), MuiAlign::Center, MuiAlign::Start);
        y += lineHeight;
    }
}

namespace {

    // A movie of the game over the whole screen; any tap skips it.
    class MovieScreen : public MuiScreen {
    public:
        bool skipped = false;

        MovieScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return true; }

        // Skips, as a tap.
        void back() override { skipped = true; }

        void build(MuiContext& ui) override
        {
            muiDrawMovie(ui, ui.screenRect());
            if (ui.touchable("movie.screen", ui.screenRect())) {
                skipped = true;
            }
        }
    };

    MovieScreen gMovieScreen;

} // namespace

void muiMovieScreenRun()
{
    MovieScreen& screen = gMovieScreen;
    screen.skipped = false;
    muiPush(&screen);

    // A legacy button's synthetic mouse press can outlive the menu/selector
    // which opened this movie. Arm mouse-to-skip only after that press is
    // released; touch on this screen is handled independently by its widget.
    bool mouseReady = false;

    // Steps run from the background processes of the input loop, each waits
    // for its frame's time.
    while (movieIsMobile()) {
        sharedFpsLimiter.mark();

        int movieInput = inputGetInput();
        int mouseEvent = mouseGetEvent();
        bool mouseDown = (mouseEvent & MOUSE_EVENT_ANY_BUTTON_DOWN) != 0;
        bool mouseHeld = mouseDown || (mouseEvent & MOUSE_EVENT_ANY_BUTTON_REPEAT) != 0;
        if ((movieInput != -1 && movieInput != -2)
            || screen.skipped
            || _game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE
            || (mouseReady && mouseDown)) {
            break;
        }
        if (!mouseHeld) {
            mouseReady = true;
        }

        devAutotestTick();

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
}

} // namespace fallout
