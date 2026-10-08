#include "mui.h"

#include <algorithm>
#include <vector>

#include "dbox.h"
#include "game.h"
#include "game_sound.h"
#include "kb.h"
#include "message.h"

namespace fallout {

namespace {

    // `showDialogBox` as a mobile UI screen: card in the middle of the screen
    // with title, text and one or two buttons.
    class DialogBoxScreen : public MuiScreen {
    public:
        std::u32string title;
        std::vector<std::u32string> body;
        std::u32string primaryLabel;
        std::u32string secondaryLabel;
        bool hasButtons = true;
        bool hasSecondary = false;

        int result = 0;

        void finish(int value)
        {
            result = value;
            finished = true;
        }

        void back() override
        {
            finish(0);
        }

        void key(int keyCode) override
        {
            if (keyCode == KEY_RETURN) {
                soundPlayFile("ib1p1xx1");
                finish(1);
            } else if (hasSecondary && (keyCode == KEY_UPPERCASE_N || keyCode == KEY_LOWERCASE_N)) {
                finish(0);
            } else if (hasSecondary && (keyCode == KEY_UPPERCASE_Y || keyCode == KEY_LOWERCASE_Y)) {
                finish(1);
            }
        }

        void build(MuiContext& ui) override
        {
            // Same as the original: quitting the game answers "yes".
            if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
                finish(1);
            }

            const MuiTheme& theme = muiTheme();
            ui.dim();

            MuiRect safe = ui.safeRect();
            float padding = ui.dp(theme.padding);
            float width = std::min(safe.w - ui.dp(32.0f), ui.dp(560.0f));
            float textWidth = width - padding * 2.0f;
            float titleSize = ui.dp(theme.titleSize);
            float bodySize = ui.dp(theme.bodySize);

            std::vector<std::u32string> titleLines;
            if (!title.empty()) {
                titleLines = muiWrapText(title, textWidth, titleSize);
            }

            std::vector<std::u32string> bodyLines;
            for (const std::u32string& line : body) {
                std::vector<std::u32string> wrapped = muiWrapText(line, textWidth, bodySize);
                bodyLines.insert(bodyLines.end(), wrapped.begin(), wrapped.end());
            }

            float titleLineHeight = muiLineHeight(titleSize);
            float bodyLineHeight = muiLineHeight(bodySize);
            float buttonHeight = ui.dp(theme.buttonHeight);

            float height = padding * 2.0f
                + titleLines.size() * titleLineHeight
                + (!titleLines.empty() && !bodyLines.empty() ? ui.dp(theme.gap) : 0.0f)
                + bodyLines.size() * bodyLineHeight
                + (hasButtons ? ui.dp(theme.gap) * 1.5f + buttonHeight : 0.0f);
            height = std::min(height, safe.h - ui.dp(16.0f));

            // Slides up a little while appearing.
            float appear = ui.appear();
            MuiRect card = {
                safe.x + (safe.w - width) / 2.0f,
                safe.y + (safe.h - height) / 2.0f + (1.0f - appear) * ui.dp(12.0f),
                width,
                height,
            };
            ui.panel(card);

            float y = card.y + padding;
            for (const std::u32string& line : titleLines) {
                muiDrawTextAligned(line, { card.x + padding, y, textWidth, titleLineHeight }, titleSize, theme.accent, MuiAlign::Center, MuiAlign::Start);
                y += titleLineHeight;
            }

            if (!titleLines.empty() && !bodyLines.empty()) {
                y += ui.dp(theme.gap);
            }

            float textBottom = card.bottom() - padding - (hasButtons ? buttonHeight + ui.dp(theme.gap) * 1.5f : 0.0f);
            for (const std::u32string& line : bodyLines) {
                if (y + bodyLineHeight > textBottom + 1.0f) {
                    break;
                }
                muiDrawTextAligned(line, { card.x + padding, y, textWidth, bodyLineHeight }, bodySize, theme.text, MuiAlign::Center, MuiAlign::Start);
                y += bodyLineHeight;
            }

            if (hasButtons) {
                MuiRect row = { card.x + padding, card.bottom() - padding - buttonHeight, textWidth, buttonHeight };
                if (hasSecondary) {
                    float gap = ui.dp(theme.gap);
                    float half = (row.w - gap) / 2.0f;
                    if (ui.button("dialog.secondary", { row.x, row.y, half, row.h }, secondaryLabel)) {
                        finish(0);
                    }
                    if (ui.button("dialog.primary", { row.x + half + gap, row.y, half, row.h }, primaryLabel, MuiButtonStyle::Primary)) {
                        finish(1);
                    }
                } else if (ui.button("dialog.primary", row, primaryLabel, MuiButtonStyle::Primary)) {
                    finish(1);
                }
            } else if (ui.tappedOutside({ 0, 0, 0, 0 })) {
                // Message without buttons: any tap closes it.
                finish(1);
            }
        }
    };

} // namespace

int muiShowDialogBox(const char* title, const char** body, int bodyLength, const char* secondaryButtonText, int flags)
{
    DialogBoxScreen screen;
    if (title != nullptr) {
        screen.title = muiDecodeGameText(title);
    }

    for (int index = 0; index < bodyLength; index++) {
        screen.body.push_back(muiDecodeGameText(body[index]));
    }

    bool yesNo = (flags & DIALOG_BOX_YES_NO) != 0;
    screen.hasButtons = yesNo || (flags & DIALOG_BOX_NO_BUTTONS) == 0;
    screen.hasSecondary = yesNo || secondaryButtonText != nullptr;

    // Button texts: 100 - DONE, 101 - YES, 102 - NO.
    MessageList messageList;
    if (messageListInit(&messageList)) {
        if (messageListLoad(&messageList, "game\\DBOX.MSG")) {
            MessageListItem item;
            item.num = yesNo ? 101 : 100;
            if (messageListGetItem(&messageList, &item)) {
                screen.primaryLabel = muiDecodeGameText(item.text);
            }

            if (yesNo) {
                item.num = 102;
                if (messageListGetItem(&messageList, &item)) {
                    screen.secondaryLabel = muiDecodeGameText(item.text);
                }
            }
        }
        messageListFree(&messageList);
    }

    if (screen.primaryLabel.empty()) {
        screen.primaryLabel = yesNo ? U"YES" : U"DONE";
    }

    if (secondaryButtonText != nullptr) {
        screen.secondaryLabel = muiDecodeGameText(secondaryButtonText);
    } else if (screen.secondaryLabel.empty()) {
        screen.secondaryLabel = U"NO";
    }

    muiRunModal(&screen);
    return screen.result;
}

} // namespace fallout
