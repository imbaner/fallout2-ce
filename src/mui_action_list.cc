#include "mui_screens.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "hud_layout.h"
#include "object.h"
#include "svga.h"
#include "touch_hud.h"
#include "world_view.h"

namespace fallout {

namespace {

    bool intersects(const MuiRect& a, const MuiRect& b)
    {
        return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
    }

    // Popup list of actions, see `muiShowActionList`.
    class ActionListScreen : public MuiScreen {
    public:
        ActionListScreen()
        {
            modal = true;
        }

        bool isActive() override
        {
            if (open && keepOpen && !keepOpen()) {
                open = false;
            }
            return open;
        }

        void back() override
        {
            close(-1);
        }

        void build(MuiContext& ui) override;

        bool open = false;
        std::string id;
        std::vector<MuiActionItem> items;
        MuiRect anchor;
        MuiRect avoid;
        std::function<void(int)> onChoose;
        std::function<bool()> keepOpen;

        // Synchronous use (`muiChooseAction`): the chosen index, -1.
        bool blocking = false;
        int result = -1;

        void close(int chosen)
        {
            open = false;
            result = chosen;
            if (blocking) {
                finished = true;
            }
        }
    };

    ActionListScreen gActionListScreen;

    void ActionListScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();

        float rowHeight = ui.dp(44.0f);
        float iconSize = ui.dp(20.0f);
        float padding = ui.dp(12.0f);
        float textSize = ui.dp(15.0f);
        float inset = ui.dp(6.0f);

        float labelWidth = 0.0f;
        float detailWidth = 0.0f;
        for (const MuiActionItem& item : items) {
            labelWidth = std::max(labelWidth, muiTextWidth(item.label, textSize));
            detailWidth = std::max(detailWidth, muiTextWidth(item.detail, textSize));
        }
        if (detailWidth > 0.0f) {
            labelWidth += padding + detailWidth;
        }

        MuiRect safe = ui.safeRect();
        float width = std::clamp(inset * 2.0f + padding + iconSize + padding + labelWidth + padding, ui.dp(180.0f), safe.w * 0.6f);

        // Rows get a bit lower to fit all of them (skills), then it scrolls.
        float maxHeight = safe.h - ui.dp(16.0f);
        if (!items.empty()) {
            rowHeight = std::clamp((maxHeight - inset * 2.0f) / items.size(), ui.dp(38.0f), rowHeight);
        }
        float contentHeight = items.size() * rowHeight;
        float height = std::min(contentHeight + inset * 2.0f, maxHeight);

        MuiRect panel = muiPlacePopup(ui, anchor, avoid, width, height);
        float appear = ui.appear();
        Uint8 alpha = static_cast<Uint8>(255 * std::min(appear * 1.5f, 1.0f));

        muiFillRoundRect(panel, ui.dp(theme.radius), theme.panel.withAlpha(alpha));
        muiStrokeRoundRect(panel, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.accent.withAlpha(alpha));
        ui.region(panel);

        MuiRect area = panel.inset(0.0f, inset);
        float offset = ui.scroll(id + ".scroll", area, contentHeight);
        bool scrolling = ui.isScrolling(id + ".scroll");

        int chosen = -1;
        muiPushClip(area);
        for (int index = 0; index < static_cast<int>(items.size()); index++) {
            MuiRect row = { area.x + inset, area.y - offset + index * rowHeight, area.w - inset * 2.0f, rowHeight };
            if (row.bottom() < area.y || row.y > area.bottom()) {
                continue;
            }

            bool enabled = items[index].enabled;
            bool pressed;
            bool tapped = ui.touchable(id + ".scroll." + std::to_string(index), row, &pressed) && enabled;
            pressed = pressed && enabled;
            ui.mark(id + "." + std::to_string(index), row);

            if (pressed) {
                muiFillRoundRect(row, ui.dp(6.0f), theme.buttonPressed.withAlpha(alpha));
            }
            if (index > 0) {
                muiFillRect({ row.x + padding, row.y, row.w - padding * 2.0f, ui.dp(1.0f) }, theme.panelBorder.withAlpha(alpha));
            }

            MuiColor color = pressed ? theme.buttonText : (enabled ? theme.accent : theme.textDim);
            muiDrawIcon(items[index].icon, row.x + padding + iconSize / 2.0f, row.centerY(), iconSize, ui.dp(1.7f), color.withAlpha(alpha));
            MuiRect label = { row.x + padding + iconSize + padding, row.y, row.right() - row.x - padding * 3.0f - iconSize, row.h };
            muiDrawTextAligned(items[index].label, label, textSize, (pressed ? theme.buttonText : (enabled ? theme.text : theme.textDim)).withAlpha(alpha), MuiAlign::Start, MuiAlign::Center);
            if (!items[index].detail.empty()) {
                muiDrawTextAligned(items[index].detail, label, textSize, (pressed ? theme.buttonText : theme.textDim).withAlpha(alpha), MuiAlign::End, MuiAlign::Center);
            }

            if (tapped && !scrolling) {
                chosen = index;
            }
        }
        muiPopClip();

        if (chosen != -1) {
            close(chosen);
            if (onChoose) {
                onChoose(chosen);
            }
        } else if (ui.tappedOutside(panel)) {
            close(-1);
        }
    }

} // namespace

// Right of the anchor, left of it, below, above: the first one inside the
// screen which doesn't cover what the popup is for.
MuiRect muiPlacePopup(MuiContext& ui, const MuiRect& anchor, const MuiRect& avoid, float width, float height)
{
    MuiRect safe = ui.safeRect().inset(ui.dp(8.0f));
    float gap = ui.dp(8.0f);

    auto clampX = [&](float x) { return std::clamp(x, safe.x, std::max(safe.right() - width, safe.x)); };
    auto clampY = [&](float y) { return std::clamp(y, safe.y, std::max(safe.bottom() - height, safe.y)); };

    const MuiRect candidates[4] = {
        { anchor.right() + gap, clampY(anchor.centerY() - height / 2.0f), width, height },
        { anchor.x - gap - width, clampY(anchor.centerY() - height / 2.0f), width, height },
        { clampX(anchor.centerX() - width / 2.0f), anchor.bottom() + gap, width, height },
        { clampX(anchor.centerX() - width / 2.0f), anchor.y - gap - height, width, height },
    };

    auto inside = [&](const MuiRect& rect) {
        return rect.x >= safe.x && rect.right() <= safe.right() && rect.y >= safe.y && rect.bottom() <= safe.bottom();
    };

    for (const MuiRect& rect : candidates) {
        if (inside(rect) && !intersects(rect, avoid)) {
            return rect;
        }
    }

    for (const MuiRect& rect : candidates) {
        if (inside(rect)) {
            return rect;
        }
    }

    return { clampX(candidates[0].x), clampY(candidates[0].y), width, height };
}

bool muiObjectRect(Object* object, MuiRect* rect)
{
    if (object == nullptr) {
        return false;
    }

    Rect world;
    objectGetRect(object, &world);

    int left;
    int top;
    int right;
    int bottom;
    worldViewWorldToScreen(world.left, world.top, &left, &top);
    worldViewWorldToScreen(world.right, world.bottom, &right, &bottom);

    int outputWidth = 0;
    int outputHeight = 0;
    if (gSdlRenderer == nullptr || SDL_GetRendererOutputSize(gSdlRenderer, &outputWidth, &outputHeight) != 0 || screenGetWidth() <= 0) {
        return false;
    }

    float scale = static_cast<float>(outputWidth) / screenGetWidth();
    *rect = { left * scale, top * scale, (right - left + 1) * scale, (bottom - top + 1) * scale };
    return true;
}

void muiPopupAnchor(Object* target, HudElementId fallback, MuiRect* anchor, MuiRect* avoid)
{
    MuiRect rect;
    if (muiObjectRect(target, &rect) || touchHudGetElementRect(fallback, &rect)) {
        *anchor = rect;
        *avoid = rect;
        return;
    }

    *anchor = MuiRect();
    *avoid = MuiRect();
}

int muiChooseAction(const std::string& id, const std::vector<MuiActionItem>& items, const MuiRect& anchor, const MuiRect& avoid)
{
    ActionListScreen& screen = gActionListScreen;
    screen.id = id;
    screen.items = items;
    screen.anchor = anchor;
    screen.avoid = avoid;
    screen.onChoose = nullptr;
    screen.keepOpen = nullptr;
    screen.blocking = true;
    screen.result = -1;
    screen.open = true;

    muiRunModal(&screen);

    screen.blocking = false;
    screen.open = false;
    return screen.result;
}

void muiShowActionList(const std::string& id, const std::vector<MuiActionItem>& items, const MuiRect& anchor, const MuiRect& avoid, std::function<void(int)> onChoose, std::function<bool()> keepOpen)
{
    ActionListScreen& screen = gActionListScreen;
    screen.blocking = false;
    screen.id = id;
    screen.items = items;
    screen.anchor = anchor;
    screen.avoid = avoid;
    screen.onChoose = std::move(onChoose);
    screen.keepOpen = std::move(keepOpen);
    screen.open = true;

    // On top of the screen it was opened from.
    muiPush(&screen);
}

void muiCloseActionList()
{
    // A blocking list returns "nothing chosen".
    gActionListScreen.close(-1);
}

bool muiIsActionListOpen(const std::string& id)
{
    return gActionListScreen.open && gActionListScreen.id == id;
}

} // namespace fallout
