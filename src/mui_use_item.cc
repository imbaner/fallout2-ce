#include "mui_screens.h"

#include <algorithm>
#include <string>
#include <vector>

#include "game.h"
#include "object.h"

namespace fallout {

namespace {

    const MuiColor kListBackground = muiRgb(0x0B120D);

    // "Use item on": dude's items (not the equipped ones) in a compact panel
    // next to the target - name, filters, a grid of three columns. Tap uses
    // the item on the target, long press shows its name, tap outside
    // cancels.
    class UseItemScreen : public MuiScreen {
    public:
        UseItemScreen()
        {
            modal = true;
        }

        void back() override
        {
            result = nullptr;
            finished = true;
        }

        void build(MuiContext& ui) override;

        Object* target = nullptr;
        MuiRect anchor;
        MuiRect avoid;
        MuiItemFilterState filter;
        Object* result = nullptr;
        Object* tooltipItem = nullptr;
    };

    void UseItemScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            back();
        }

        std::vector<MuiListItem> items;
        for (const MuiListItem& entry : muiInventoryItems(gDude, filter)) {
            if ((entry.item->flags & OBJECT_EQUIPPED) == 0) {
                items.push_back(entry);
            }
        }

        constexpr int kColumns = 3;
        float padding = ui.dp(6.0f);
        float gap = ui.dp(4.0f);
        float cell = ui.dp(50.0f);
        float gridWidth = cell * kColumns + gap * (kColumns + 1);
        float width = gridWidth + padding * 2.0f;
        float titleHeight = ui.dp(20.0f);
        float filterHeight = ui.dp(28.0f);
        int rows = std::max((static_cast<int>(items.size()) + kColumns - 1) / kColumns, 1);
        float contentHeight = rows * (cell + gap);
        float fixedHeight = padding + titleHeight + ui.dp(4.0f) + filterHeight + ui.dp(6.0f) + gap * 2.0f + padding;
        float height = std::min(fixedHeight + contentHeight, ui.safeRect().h - ui.dp(16.0f));

        MuiRect panel = muiPlacePopup(ui, anchor, avoid, width, height);
        muiFillRoundRect(panel, ui.dp(theme.radius), theme.panel);
        muiStrokeRoundRect(panel, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.accent);
        ui.region(panel);

        float x = panel.x + padding;
        float y = panel.y + padding;

        // Who the item is for.
        MuiRect title = { x, y, gridWidth, titleHeight };
        muiPushClip(title);
        muiDrawTextAligned(muiDecodeGameText(objectGetName(target)), title, ui.dp(13.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        muiPopClip();
        y += titleHeight + ui.dp(4.0f);

        muiItemFilterRow(ui, "useitem.filter", { x, y, gridWidth, filterHeight }, &filter);
        y += filterHeight + ui.dp(6.0f);

        MuiRect list = { x, y, gridWidth, panel.bottom() - padding - y };
        muiFillRoundRect(list, ui.dp(6.0f), kListBackground);
        muiStrokeRoundRect(list, ui.dp(6.0f), ui.dp(1.0f), theme.panelBorder);

        MuiRect area = list.inset(gap);
        float offset = ui.scroll("useitem.list", area, contentHeight);
        bool scrolling = ui.isScrolling("useitem.list");

        MuiRect tooltipCell;
        bool tooltip = false;

        muiPushClip(area);
        for (int index = 0; index < static_cast<int>(items.size()); index++) {
            MuiRect cellRect = { area.x + (index % kColumns) * (cell + gap), area.y - offset + (index / kColumns) * (cell + gap), cell, cell };
            if (cellRect.bottom() < area.y || cellRect.y > area.bottom()) {
                continue;
            }

            MuiRect touchRect = cellRect;
            touchRect.y = std::max(cellRect.y, area.y);
            touchRect.h = std::min(cellRect.bottom(), area.bottom()) - touchRect.y;

            Object* item = items[index].item;
            ui.mark("useitem.pid." + std::to_string(item->pid), touchRect);

            bool pressed;
            bool longPressed;
            bool tapped = ui.touchable("useitem.list." + std::to_string(index), touchRect, &pressed, &longPressed);
            muiDrawItemCell(ui, cellRect, item, items[index].quantity, pressed);

            // Long press: the item's name while held.
            if (longPressed) {
                tooltipItem = item;
            }
            if (tooltipItem == item && pressed) {
                tooltip = true;
                tooltipCell = cellRect;
            }

            if (tapped && !scrolling) {
                result = item;
                finished = true;
            }
        }
        muiPopClip();

        if (!ui.pointerDown()) {
            tooltipItem = nullptr;
        }

        if (tooltip) {
            std::u32string name = muiDecodeGameText(objectGetName(tooltipItem));
            float size = ui.dp(13.0f);
            float bubbleWidth = muiTextWidth(name, size) + ui.dp(16.0f);
            float bubbleHeight = ui.dp(26.0f);
            MuiRect bubble = { std::clamp(tooltipCell.centerX() - bubbleWidth / 2.0f, ui.safeRect().x, ui.safeRect().right() - bubbleWidth), tooltipCell.y - bubbleHeight - ui.dp(6.0f), bubbleWidth, bubbleHeight };
            muiFillRoundRect(bubble, ui.dp(6.0f), theme.panel);
            muiStrokeRoundRect(bubble, ui.dp(6.0f), ui.dp(1.0f), theme.accent);
            muiDrawTextAligned(name, bubble, size, theme.text, MuiAlign::Center, MuiAlign::Center);
        }

        if (!finished && ui.tappedOutside(panel)) {
            back();
        }
    }

} // namespace

Object* muiChooseItemToUse(Object* target)
{
    UseItemScreen screen;
    screen.target = target;
    muiPopupAnchor(target, HudElementId::Inventory, &(screen.anchor), &(screen.avoid));

    muiRunModal(&screen);

    return screen.result;
}

} // namespace fallout
