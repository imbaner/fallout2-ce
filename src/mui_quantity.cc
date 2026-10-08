#include "mui_screens.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game.h"
#include "kb.h"
#include "message.h"
#include "object.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg`, English fallbacks for data without them.
    constexpr int kTextAll = 121;
    constexpr int kTextCancel = 122;
    constexpr int kTextDone = 123;

    // How many items to move (or explosive timer): slider, -/+ for exact
    // values, all, cancel, done.
    class QuantityScreen : public MuiScreen {
    public:
        Object* item = nullptr;
        int minValue = 1;
        int maxValue = 1;
        int step = 1;
        int value = 1;
        bool timer = false;
        int result = -1;

        MessageList messageList;
        bool messageListLoaded = false;

        void back() override
        {
            result = -1;
            finished = true;
        }

        void key(int keyCode) override
        {
            if (keyCode == KEY_RETURN) {
                result = value;
                finished = true;
            }
        }

        void build(MuiContext& ui) override;

        const char* text(int id, const char* fallback)
        {
            if (messageListLoaded) {
                MessageListItem entry;
                entry.num = id;
                if (messageListGetItem(&messageList, &entry)) {
                    return entry.text;
                }
            }
            return fallback;
        }

    private:
        std::u32string format(int amount) const
        {
            char buffer[32];
            if (timer) {
                snprintf(buffer, sizeof(buffer), "%d:%02d", amount / 60, amount % 60);
            } else {
                snprintf(buffer, sizeof(buffer), "%d", amount);
            }
            return muiDecodeUtf8(buffer);
        }
    };

    void QuantityScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            back();
        }

        ui.dim();

        MuiRect safe = ui.safeRect();
        float padding = ui.dp(16.0f);
        float width = std::min(safe.w - ui.dp(32.0f), ui.dp(440.0f));
        float buttonHeight = ui.dp(46.0f);
        float cellSize = ui.dp(64.0f);
        float valueSize = ui.dp(28.0f);
        float height = padding * 2.0f + cellSize + ui.dp(8.0f) + muiLineHeight(valueSize) + ui.dp(8.0f) + buttonHeight + ui.dp(12.0f) + buttonHeight;

        MuiRect card = { safe.x + (safe.w - width) / 2.0f, safe.y + (safe.h - height) / 2.0f + (1.0f - ui.appear()) * ui.dp(12.0f), width, height };
        ui.panel(card);

        MuiRect content = card.inset(padding);
        float y = content.y;

        // Item and the available amount.
        if (item != nullptr) {
            muiDrawItemCell(ui, { content.x, y, cellSize, cellSize }, item, 1, false);
            MuiRect nameRect = { content.x + cellSize + ui.dp(12.0f), y, content.w - cellSize - ui.dp(12.0f), cellSize / 2.0f };
            muiDrawTextAligned(muiDecodeGameText(objectGetName(item)), nameRect, ui.dp(17.0f), theme.accent, MuiAlign::Start, MuiAlign::End);
            if (!timer) {
                MuiRect totalRect = { nameRect.x, y + cellSize / 2.0f, nameRect.w, cellSize / 2.0f };
                muiDrawTextAligned(U"x" + format(maxValue), totalRect, ui.dp(14.0f), theme.textDim, MuiAlign::Start, MuiAlign::Start);
            }
        }
        y += cellSize + ui.dp(8.0f);

        muiDrawTextAligned(format(value), { content.x, y, content.w, muiLineHeight(valueSize) }, valueSize, theme.text, MuiAlign::Center, MuiAlign::Start);
        y += muiLineHeight(valueSize) + ui.dp(8.0f);

        // - slider +
        MuiRect minus = { content.x, y, buttonHeight, buttonHeight };
        MuiRect plus = { content.right() - buttonHeight, y, buttonHeight, buttonHeight };
        if (ui.button("quantity.minus", minus, U"−")) {
            value = std::max(value - step, minValue);
        }
        if (ui.button("quantity.plus", plus, U"+")) {
            value = std::min(value + step, maxValue);
        }

        MuiRect sliderRect = { minus.right() + ui.dp(18.0f), y, plus.x - ui.dp(18.0f) - minus.right() - ui.dp(18.0f), buttonHeight };
        float raw = ui.slider("quantity.slider", sliderRect, static_cast<float>(value), static_cast<float>(minValue), static_cast<float>(maxValue));
        int snapped = minValue + static_cast<int>(std::lround((raw - minValue) / step)) * step;
        value = std::clamp(snapped, minValue, maxValue);
        y += buttonHeight + ui.dp(12.0f);

        // All (not for the timer), cancel, done.
        float gap = ui.dp(8.0f);
        int count = timer ? 2 : 3;
        float buttonWidth = (content.w - gap * (count - 1)) / count;
        float x = content.x;
        if (!timer) {
            if (ui.button("quantity.all", { x, y, buttonWidth, buttonHeight }, muiDecodeGameText(text(kTextAll, "ALL")))) {
                value = maxValue;
            }
            x += buttonWidth + gap;
        }

        if (ui.button("quantity.cancel", { x, y, buttonWidth, buttonHeight }, muiDecodeGameText(text(kTextCancel, "CANCEL")))) {
            back();
        }
        x += buttonWidth + gap;

        if (ui.button("quantity.done", { x, y, buttonWidth, buttonHeight }, muiDecodeGameText(text(kTextDone, "DONE")), MuiButtonStyle::Primary)) {
            result = value;
            finished = true;
        }
    }

} // namespace

int muiQuantitySelect(Object* item, int minValue, int maxValue, int step, int defaultValue, bool timer)
{
    if (maxValue < minValue) {
        return -1;
    }

    QuantityScreen screen;
    screen.item = item;
    screen.minValue = minValue;
    screen.maxValue = maxValue;
    screen.step = std::max(step, 1);
    screen.value = std::clamp(defaultValue, minValue, maxValue);
    screen.timer = timer;

    if (messageListInit(&(screen.messageList))) {
        screen.messageListLoaded = messageListLoad(&(screen.messageList), "game\\ce.msg");
    }

    muiRunModal(&screen);

    if (screen.messageListLoaded) {
        messageListFree(&(screen.messageList));
    }

    return screen.result;
}

} // namespace fallout
