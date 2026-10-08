#include "mui_notify.h"

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include "touch_hud.h"

namespace fallout {

namespace {

    const MuiColor kPlate = muiRgb(0x0B120D, 245);
    const MuiColor kHudLine = muiRgb(0x0B120D, 200);
    const MuiColor kWarning = muiRgb(0xFF6E5E);

    // Shown for a base time plus time to read it, fading out at the end.
    constexpr unsigned int kBaseMs = 2500;
    constexpr unsigned int kPerCharacterMs = 45;
    constexpr unsigned int kMaxMs = 9000;
    constexpr unsigned int kHudMs = 4000;
    constexpr unsigned int kFadeMs = 300;

    // Plates at once, lines of one plate; lines next to the HUD's log button.
    constexpr size_t kMaxPlates = 3;
    constexpr size_t kMaxPlateLines = 5;
    constexpr size_t kMaxHudLines = 4;

    struct Notice {
        std::string text;
        std::string title;
        MuiNoticeKind kind;
        // First frame it was seen (0 - not yet): time counts from there.
        unsigned int shownTime = 0;
    };

    std::deque<Notice> gNotices;

    // HUD's area this frame (see `muiNotifyHudArea`, the HUD is drawn before
    // the notifications).
    bool gHudThisFrame = false;
    MuiRect gHudArea;
    bool gHudLogOpen = false;

    unsigned int duration(const Notice& notice, bool hud)
    {
        if (hud) {
            return kHudMs;
        }
        return std::min(kBaseMs + static_cast<unsigned int>(notice.text.size()) * kPerCharacterMs, kMaxMs);
    }

    float fade(const Notice& notice, unsigned int now, bool hud)
    {
        unsigned int end = notice.shownTime + duration(notice, hud);
        if (now >= end) {
            return 0.0f;
        }
        return std::min(1.0f, static_cast<float>(end - now) / kFadeMs);
    }

    MuiColor faded(MuiColor color, float alpha)
    {
        return color.withAlpha(static_cast<Uint8>(color.a * alpha));
    }

    // Log lines next to the HUD's log button, newest at the bottom.
    void drawHud(MuiContext& ui, const std::vector<Notice*>& notices)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(13.0f);
        float padding = ui.dp(6.0f);
        const MuiRect& area = gHudArea;

        struct Line {
            std::u32string text;
            float alpha;
        };

        std::vector<Line> lines;
        for (Notice* notice : notices) {
            float alpha = fade(*notice, ui.now, true);
            for (const std::u32string& text : muiWrapText(muiDecodeGameText(notice->text.c_str()), area.w - padding * 2.0f, size)) {
                lines.push_back({ text, alpha });
            }
        }

        if (lines.size() > kMaxHudLines) {
            lines.erase(lines.begin(), lines.end() - kMaxHudLines);
        }

        float rowHeight = muiLineHeight(size) + padding;
        float y = area.bottom() - lines.size() * (rowHeight + ui.dp(2.0f));
        for (const Line& line : lines) {
            float width = std::min(muiTextWidth(line.text, size) + padding * 2.0f, area.w);
            muiFillRoundRect({ area.x, y, width, rowHeight }, ui.dp(4.0f), faded(kHudLine, line.alpha));
            muiDrawTextAligned(line.text, { area.x + padding, y, width, rowHeight }, size, faded(theme.text, line.alpha), MuiAlign::Start, MuiAlign::Center);
            y += rowHeight + ui.dp(2.0f);
        }
    }

    // Plates at the top center, newest at the bottom of the stack.
    void drawPlates(MuiContext& ui, const std::vector<Notice*>& notices)
    {
        const MuiTheme& theme = muiTheme();
        MuiRect safe = ui.safeRect().inset(ui.dp(10.0f));
        float maxWidth = std::min(safe.w * 0.6f, ui.dp(480.0f));
        float size = ui.dp(14.0f);
        float titleSize = ui.dp(12.0f);
        float paddingX = ui.dp(14.0f);
        float paddingY = ui.dp(8.0f);
        float lineHeight = muiLineHeight(size);
        float y = safe.y;

        for (Notice* notice : notices) {
            float alpha = fade(*notice, ui.now, false);

            std::vector<std::u32string> lines = muiWrapText(muiDecodeGameText(notice->text.c_str()), maxWidth - paddingX * 2.0f, size);
            if (lines.size() > kMaxPlateLines) {
                lines.resize(kMaxPlateLines);
                lines.back() += U"...";
            }

            std::u32string title = muiDecodeGameText(notice->title.c_str());
            float width = title.empty() ? 0.0f : muiTextWidth(title, titleSize);
            for (const std::u32string& line : lines) {
                width = std::max(width, muiTextWidth(line, size));
            }
            width = std::min(width + paddingX * 2.0f, maxWidth);

            float titleHeight = title.empty() ? 0.0f : muiLineHeight(titleSize) + ui.dp(2.0f);
            float height = paddingY * 2.0f + titleHeight + lines.size() * lineHeight;
            MuiRect plate = { safe.centerX() - width / 2.0f, y, width, height };
            y += height + ui.dp(6.0f);

            MuiColor border = theme.panelBorder;
            if (notice->kind == MuiNoticeKind::Warning) {
                border = kWarning;
            } else if (notice->kind == MuiNoticeKind::Reply) {
                border = theme.accent;
            }

            muiFillRoundRect(plate, ui.dp(8.0f), faded(kPlate, alpha));
            muiStrokeRoundRect(plate, ui.dp(8.0f), ui.dp(1.2f), faded(border, alpha));

            float textY = plate.y + paddingY;
            if (!title.empty()) {
                muiDrawTextAligned(title, { plate.x + paddingX, textY, plate.w - paddingX * 2.0f, muiLineHeight(titleSize) }, titleSize, faded(theme.textDim, alpha), MuiAlign::Start, MuiAlign::Start);
                textY += titleHeight;
            }

            MuiColor textColor = notice->kind == MuiNoticeKind::Warning ? kWarning : theme.text;
            for (const std::u32string& line : lines) {
                muiDrawTextAligned(line, { plate.x + paddingX, textY, plate.w - paddingX * 2.0f, lineHeight }, size, faded(textColor, alpha), lines.size() == 1 && title.empty() ? MuiAlign::Center : MuiAlign::Start, MuiAlign::Start);
                textY += lineHeight;
            }
        }
    }

} // namespace

void muiNotify(const char* text, MuiNoticeKind kind, const char* title)
{
    if (!muiIsEnabled() || text == nullptr || text[0] == '\0') {
        return;
    }

    Notice notice;
    notice.text = text;
    notice.title = title != nullptr ? title : "";
    notice.kind = kind;

    // The same again (repeated taps): shown once, from now.
    for (auto it = gNotices.begin(); it != gNotices.end(); ++it) {
        if (it->text == notice.text && it->kind == notice.kind && it->title == notice.title) {
            gNotices.erase(it);
            break;
        }
    }

    gNotices.push_back(notice);
}

void muiNotifyClear()
{
    gNotices.clear();
}

void muiNotifyHudArea(const MuiRect& area, bool logOpen)
{
    gHudThisFrame = true;
    gHudArea = area;
    gHudLogOpen = logOpen;
}

void muiNotifyRender(MuiContext& ui)
{
    bool hudShown = gHudThisFrame && !muiCoversScreen();
    gHudThisFrame = false;

    // Where the log is seen: the HUD's log, or the game's interface bar when
    // the touch HUD is off and no screen covers it.
    bool gameLogShown = !touchHudIsEnabled() && !muiCoversScreen();

    std::vector<Notice*> hud;
    std::vector<Notice*> plates;
    for (Notice& notice : gNotices) {
        if (notice.kind == MuiNoticeKind::Log) {
            if (gameLogShown || (hudShown && gHudLogOpen)) {
                // Seen in the log itself.
                if (notice.shownTime == 0) {
                    notice.shownTime = ui.now - kMaxMs;
                }
                continue;
            }

            if (hudShown) {
                if (notice.shownTime == 0) {
                    notice.shownTime = ui.now;
                }
                hud.push_back(&notice);
                continue;
            }
        }

        if (notice.shownTime == 0 && plates.size() < kMaxPlates) {
            notice.shownTime = ui.now;
        }
        if (notice.shownTime != 0) {
            plates.push_back(&notice);
        }
    }

    if (!hud.empty()) {
        drawHud(ui, hud);
    }
    if (!plates.empty()) {
        drawPlates(ui, plates);
    }

    // Gone ones.
    gNotices.erase(std::remove_if(gNotices.begin(), gNotices.end(), [&](const Notice& notice) {
        bool isHud = notice.kind == MuiNoticeKind::Log && hudShown;
        return notice.shownTime != 0 && ui.now >= notice.shownTime + duration(notice, isHud);
    }),
        gNotices.end());
}

} // namespace fallout
