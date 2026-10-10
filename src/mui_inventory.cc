#include "mui_screens.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "game_mouse.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "kb.h"
#include "mui_icons.h"
#include "object.h"
#include "proto_types.h"
#include "stat.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg`.
    constexpr int kTextDrop = 147;
    constexpr int kTextDropAll = 156;
    constexpr int kTextSure = 157;

    // Taps on "Drop all" within this time confirm it.
    constexpr unsigned int kConfirmMs = 3000;

    const MuiColor kBackground = muiRgb(0x07100A);
    const MuiColor kListBackground = muiRgb(0x0B120D);
    const MuiColor kDropZone = muiRgb(0x1A2A10);
    const MuiColor kWarning = muiRgb(0xFFB000);
    const MuiColor kEncumbered = muiRgb(0xFF4A3A);

    // Glow over what an action changed, ms.
    constexpr unsigned int kFlashMs = 800;

    // Finger movement per character turn step, dp.
    constexpr float kTurnStepDp = 28.0f;

    // Drag payload: list index, or slot.
    constexpr int kPayloadSlotBase = 100000;

    std::u32string number(int value)
    {
        return muiDecodeUtf8(std::to_string(value).c_str());
    }

    std::u32string trimmed(const std::string& text)
    {
        size_t start = text.find_first_not_of(' ');
        return start == std::string::npos ? std::u32string() : muiDecodeGameText(text.c_str() + start);
    }

    // Inventory screen: items (filters, grid), character with armor and
    // hand slots, character summary or description of the chosen item. Tap
    // shows the description, long press opens the game's action menu,
    // dragging sideways moves items (hands, armor, into containers, ammo into
    // weapons, to the ground) or uses a consumable on the player's figure.
    // Runs over the game's inventory (see
    // `inventoryQueueAction`).
    class InventoryScreen : public MuiScreen {
    public:
        InventoryScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return true; }

        bool isActive() override
        {
            InventoryView view;
            bool active = muiIsEnabled() && inventoryGetView(&view);
            if (active && !wasActive) {
                filter = MuiItemFilterState();
                selected = nullptr;
                examinedVersion = view.examinedVersion;
                lastMenuVersion = view.lastMenuVersion;
                flash = Flash();
                dropAllTime = 0;
            }
            wasActive = active;
            return active;
        }

        void build(MuiContext& ui) override;
        void back() override;

    private:
        struct Cell {
            MuiRect rect;
            Object* item;
        };

        bool wasActive = false;
        MuiItemFilterState filter;
        Object* selected = nullptr;
        unsigned int examinedVersion = 0;
        unsigned int dropAllTime = 0;

        // Soft glow over what the last action changed.
        enum class FlashTarget {
            None,
            Figure,
            Slot,
            Item,
            Header,
        };

        struct Flash {
            FlashTarget target = FlashTarget::None;
            InventorySlot slot = InventorySlot::None;
            Object* item = nullptr;
            unsigned int time = 0;
        };

        Flash flash;
        unsigned int lastMenuVersion = 0;
        unsigned int now = 0;

        void startFlash(FlashTarget target, InventorySlot slot, Object* item)
        {
            flash = { target, slot, item, now };
        }

        void drawFlash(MuiContext& ui, const MuiRect& rect, bool matches);

        // Turning the character by a swipe over it.
        bool figureHeld = false;
        bool turning = false;
        float turnStartX = 0.0f;
        int turnStartRotation = 0;

        std::vector<MuiListItem> items;
        std::vector<Cell> cells;
        MuiRect listRect;
        MuiRect figureRect;
        MuiRect slotRects[3];
        MuiRect infoRect;
        MuiRect crumbRect;

        // Description of the selected item, rebuilt when it may change
        // (examining runs item scripts).
        InventoryItemInfo info;
        std::string infoKey;

        void buildHeader(MuiContext& ui, const InventoryView& view, const MuiRect& rect);
        void buildList(MuiContext& ui, const MuiRect& rect);
        void buildBottom(MuiContext& ui, const MuiRect& rect);
        void buildCharacter(MuiContext& ui, const InventoryView& view, const MuiRect& rect);
        void buildSlot(MuiContext& ui, const InventoryView& view, InventorySlot slot, Object* item, const MuiRect& rect, bool active);
        void buildInfo(MuiContext& ui, const InventoryView& view, const MuiRect& rect);
        void buildSummary(MuiContext& ui, const MuiRect& content, float* y, bool draw);
        void buildItemInfo(MuiContext& ui, const MuiRect& content, float* y, bool draw);
        void handleDrop(MuiContext& ui, const InventoryView& view);
        void drawDragged(MuiContext& ui, const InventoryView& view);

        Object* draggedItem(MuiContext& ui, const InventoryView& view, InventorySlot* slot);
        void openActionMenu(MuiContext& ui, Object* item, InventorySlot slot);
        void move(Object* item, InventorySlot slot, InventoryDropTarget target, Object* targetItem);
    };

    InventoryScreen gInventoryScreen;
    bool gInventoryScreenPushed = false;

    Object* slotItem(const InventoryView& view, InventorySlot slot)
    {
        switch (slot) {
        case InventorySlot::LeftHand:
            return view.leftHand;
        case InventorySlot::RightHand:
            return view.rightHand;
        case InventorySlot::Armor:
            return view.armor;
        default:
            return nullptr;
        }
    }

    // Back: closes the open container, then the inventory.
    void InventoryScreen::back()
    {
        InventoryView view;
        if (inventoryGetView(&view) && view.container != nullptr) {
            InventoryAction action;
            action.type = InventoryActionType::CloseContainer;
            inventoryQueueAction(action);
        } else {
            inventoryRequestClose();
        }
    }

    void InventoryScreen::build(MuiContext& ui)
    {
        InventoryView view;
        if (!inventoryGetView(&view)) {
            return;
        }

        now = ui.now;

        // Look from the action menu shows the item here.
        if (view.examinedVersion != examinedVersion) {
            examinedVersion = view.examinedVersion;
            selected = view.examinedItem;
        }

        // Use from the action menu acts on the character, unload on the
        // weapon.
        if (view.lastMenuVersion != lastMenuVersion) {
            lastMenuVersion = view.lastMenuVersion;
            if (view.lastMenuAction == GAME_MOUSE_ACTION_MENU_ITEM_USE && view.container == nullptr) {
                startFlash(FlashTarget::Figure, InventorySlot::None, nullptr);
            } else if (view.lastMenuAction == GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD) {
                startFlash(FlashTarget::Item, InventorySlot::None, view.lastMenuItem);
            }
        }

        items = muiInventoryItems(view.listOwner, view.container != nullptr ? MuiItemFilterState() : filter);

        // Selected item is gone (dropped, used up).
        if (selected != nullptr && selected != view.leftHand && selected != view.rightHand && selected != view.armor) {
            bool listed = false;
            for (const MuiListItem& entry : items) {
                listed = listed || entry.item == selected;
            }
            if (!listed) {
                selected = nullptr;
            }
        }

        // Opaque over the map.
        muiFillRect(ui.screenRect(), kBackground);

        MuiRect content;
        MuiRect tabs;
        muiDialogLayout(ui, &content, &tabs);

        // Back: closes the open container, then the inventory. The other game
        // screens: the inventory closes and the game opens them.
        MuiGameScreenTab tab = muiGameScreenTabs(ui, tabs, "inventory", MuiGameScreenTab::Inventory, view.critter == gDude);
        if (tab == MuiGameScreenTab::Back) {
            back();
        } else if (tab != MuiGameScreenTab::None && muiSwitchGameScreen(ui, tab)) {
            inventoryRequestClose();
        }

        float gap = ui.dp(8.0f);
        float leftWidth = content.w * 0.4f;
        float centerWidth = content.w * 0.27f;
        MuiRect left = { content.x, content.y, leftWidth, content.h };
        MuiRect center = { left.right() + gap, content.y, centerWidth, content.h };
        MuiRect right = { center.right() + gap, content.y, content.right() - center.right() - gap, content.h };

        float rowHeight = ui.dp(30.0f);
        float bottomHeight = ui.dp(34.0f);
        buildHeader(ui, view, { left.x, left.y, left.w, rowHeight });
        buildList(ui, { left.x, left.y + rowHeight + ui.dp(6.0f), left.w, left.h - rowHeight - bottomHeight - ui.dp(12.0f) });
        buildBottom(ui, { left.x, left.bottom() - bottomHeight, left.w, bottomHeight });
        buildCharacter(ui, view, center);
        buildInfo(ui, view, right);

        handleDrop(ui, view);
        drawDragged(ui, view);
    }

    // Filters, or the open container with a way back.
    void InventoryScreen::buildHeader(MuiContext& ui, const InventoryView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        crumbRect = MuiRect();

        if (view.container == nullptr) {
            muiItemFilterRow(ui, "inventory.filter", rect, &filter);
            return;
        }

        // Dragging an item of the container over it takes the item out.
        int payload;
        bool target = ui.dragging(&payload) && payload < kPayloadSlotBase;
        crumbRect = rect;

        bool pressed;
        if (ui.touchable("inventory.container", rect, &pressed)) {
            InventoryAction action;
            action.type = InventoryActionType::CloseContainer;
            inventoryQueueAction(action);
        }

        muiFillRoundRect(rect, ui.dp(5.0f), pressed || target ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(target ? 2.0f : 1.0f), target || pressed ? theme.accent : theme.buttonBorder);
        drawFlash(ui, rect, flash.target == FlashTarget::Header);

        float iconSize = rect.h * 0.5f;
        muiDrawIcon(MuiIcon::Back, rect.x + ui.dp(8.0f) + iconSize / 2.0f, rect.centerY(), iconSize, ui.dp(1.8f), theme.accent);

        // Container name, while dragging - where the item goes.
        Object* owner = target ? view.critter : view.container;
        MuiRect label = { rect.x + iconSize + ui.dp(16.0f), rect.y, rect.w - iconSize - ui.dp(20.0f), rect.h };
        muiPushClip(label);
        muiDrawTextAligned(muiDecodeGameText(objectGetName(owner)), label, ui.dp(14.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        muiPopClip();
    }

    void InventoryScreen::buildList(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        listRect = rect;
        cells.clear();

        // Items from slots go back here.
        int dragPayload;
        bool dragging = ui.dragging(&dragPayload);
        bool target = dragging && dragPayload >= kPayloadSlotBase;
        Object* dragged = nullptr;
        if (dragging && dragPayload < kPayloadSlotBase && dragPayload < static_cast<int>(items.size())) {
            dragged = items[dragPayload].item;
        }

        muiFillRoundRect(rect, ui.dp(6.0f), kListBackground);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(target ? 2.0f : 1.0f), target ? theme.accent : theme.panelBorder);

        constexpr int kColumns = 4;
        float gap = ui.dp(4.0f);
        MuiRect area = rect.inset(gap);
        float cell = (area.w - gap * (kColumns - 1)) / kColumns;
        int rows = (static_cast<int>(items.size()) + kColumns - 1) / kColumns;
        float contentHeight = rows * (cell + gap);

        float offset = ui.scroll("inventory.list", area, contentHeight);
        bool scrolling = ui.isScrolling("inventory.list");
        // Vertical moves scroll only a list that doesn't fit.
        bool scrollable = contentHeight > area.h;

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
            cells.push_back({ touchRect, item });
            ui.mark("inventory.pid." + std::to_string(item->pid), touchRect);

            std::string id = "inventory.list." + std::to_string(index);
            bool pressed;
            bool longPressed;
            bool tapped = ui.touchable(id, touchRect, &pressed, &longPressed);
            bool isDragged = ui.dragSource(id, index, !scrollable);

            if (isDragged) {
                // Placeholder, the item follows the finger.
                muiStrokeRoundRect(cellRect, ui.dp(5.0f), ui.dp(1.0f), theme.buttonBorder);
                continue;
            }

            muiDrawItemCell(ui, cellRect, item, items[index].quantity, pressed || item == selected);
            drawFlash(ui, cellRect, flash.target == FlashTarget::Item && flash.item == item);

            // What dropping the dragged item here does: load ammo, put into
            // the container.
            if (dragged != nullptr && inventoryCanDropOnto(dragged, item)) {
                muiStrokeRoundRect(cellRect, ui.dp(5.0f), ui.dp(2.0f), theme.accent);
                MuiIcon icon = itemGetType(item) == ITEM_TYPE_CONTAINER ? MuiIcon::Inventory : MuiIcon::Reload;
                float iconSize = cell * 0.26f;
                muiFillCircle(cellRect.x + iconSize * 0.9f, cellRect.y + iconSize * 0.9f, iconSize * 0.8f, theme.buttonPrimary);
                muiDrawIcon(icon, cellRect.x + iconSize * 0.9f, cellRect.y + iconSize * 0.9f, iconSize, ui.dp(1.5f), theme.buttonPrimaryText);
            }

            if (longPressed && !scrolling) {
                openActionMenu(ui, item, InventorySlot::None);
            } else if (tapped && !scrolling) {
                selected = selected == item ? nullptr : item;
            }
        }
        muiPopClip();
    }

    // Weight of the shown items, "Drop all" of them.
    void InventoryScreen::buildBottom(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        // Every filter, "All" too: the bottom always tells what the shown
        // items weigh (the total with what is worn and held is on the right).
        float iconSize = ui.dp(15.0f);
        muiDrawIcon(MuiIcon::Weight, rect.x + iconSize / 2.0f + ui.dp(2.0f), rect.centerY(), iconSize, ui.dp(1.5f), theme.textDim);
        MuiRect weightRect = { rect.x + iconSize + ui.dp(8.0f), rect.y, rect.w * 0.4f, rect.h };
        muiDrawTextAligned(number(muiItemsWeight(items)), weightRect, ui.dp(14.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);

        bool confirming = dropAllTime != 0 && ui.now - dropAllTime < kConfirmMs;
        MuiRect button = { rect.right() - rect.w * 0.55f, rect.y, rect.w * 0.55f, rect.h };
        bool pressed;
        if (ui.touchable("inventory.drop_all", button, &pressed) && !items.empty()) {
            if (confirming) {
                InventoryAction action;
                action.type = InventoryActionType::DropAll;
                for (const MuiListItem& entry : items) {
                    action.items.push_back(entry.item);
                }
                inventoryQueueAction(action);
                dropAllTime = 0;
                confirming = false;
            } else {
                dropAllTime = ui.now;
                confirming = true;
            }
        }

        MuiColor fill = confirming ? kWarning : (pressed ? theme.buttonPressed : theme.button);
        MuiColor color = confirming ? muiRgb(0x2A1A00) : theme.accent;
        muiFillRoundRect(button, ui.dp(6.0f), fill);
        muiStrokeRoundRect(button, ui.dp(6.0f), ui.dp(1.0f), confirming ? kWarning : theme.buttonBorder);
        const char* label = confirming ? muiText(kTextSure, "SURE?") : muiText(kTextDropAll, "DROP ALL");
        MuiRect labelRect = button.inset(ui.dp(4.0f), 0.0f);
        muiPushClip(labelRect);
        muiDrawTextAligned(muiDecodeGameText(label), labelRect, ui.dp(13.0f), color, MuiAlign::Center, MuiAlign::Center);
        muiPopClip();
    }

    // Figure (turning, as in the game), armor slot, hit points, armor class,
    // weight, hand slots.
    void InventoryScreen::buildCharacter(MuiContext& ui, const InventoryView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(8.0f);

        MuiRect figure = { rect.x, rect.y, rect.w * 0.58f, rect.h * 0.52f };
        figureRect = figure;
        muiFillRoundRect(figure, ui.dp(6.0f), kListBackground);
        muiStrokeRoundRect(figure, ui.dp(6.0f), ui.dp(1.0f), theme.panelBorder);

        // Horizontal swipe turns the character the same way, tap shows the
        // character summary again.
        bool figurePressed;
        bool figureTapped = ui.touchable("inventory.figure", figure, &figurePressed);
        if (figurePressed && !turning && !figureHeld) {
            figureHeld = true;
            turnStartX = ui.pointerX();
            turnStartRotation = view.bodyRotation;
        }

        if (figureHeld) {
            float dx = ui.pointerX() - turnStartX;
            if (std::fabs(dx) > ui.dp(muiTheme().touchSlop)) {
                turning = true;
            }
            if (turning) {
                // Swipe to the right turns the front of the figure to the
                // right (rotations go clockwise).
                int steps = static_cast<int>(std::lround(dx / ui.dp(kTurnStepDp)));
                inventorySetBodyRotation(turnStartRotation - steps);
            }
            if (!ui.pointerDown()) {
                figureHeld = false;
            }
        } else if (!ui.pointerDown()) {
            turning = false;
        }

        if (figureTapped && !turning) {
            selected = nullptr;
        }

        // Turn hint under the feet.
        float turnSize = figure.w * 0.55f;
        float turnHeight = turnSize * 0.3f;
        // The ellipse's lowest point is 0.1 of the icon below its center.
        muiDrawIcon(MuiIcon::Turn, figure.centerX(), figure.bottom() - ui.dp(10.0f) - turnSize * 0.1f, turnSize, ui.dp(1.8f), turning ? theme.accent : theme.textDim);

        // One scale for all directions (fits the tallest), so the figure
        // keeps its size while turning; wide frames (spear) may cross the
        // frame.
        int maxHeight = 0;
        for (int rotation = 0; rotation < 6; rotation++) {
            int frameWidth;
            int frameHeight;
            if (muiArtTexture(view.bodyFrmId, 0, &frameWidth, &frameHeight, rotation) != nullptr) {
                maxHeight = std::max(maxHeight, frameHeight);
            }
        }

        int width;
        int height;
        SDL_Texture* texture = muiArtTexture(view.bodyFrmId, 0, &width, &height, view.bodyRotation);
        if (texture != nullptr && maxHeight > 0) {
            MuiRect area = figure.inset(ui.dp(6.0f));
            area.h -= turnHeight;
            float scale = area.h / maxHeight;
            if (scale >= 1.0f) {
                scale = std::floor(scale);
            }
            float w = width * scale;
            float h = height * scale;
            muiDrawTexture(texture, { area.centerX() - w / 2.0f, area.centerY() - h / 2.0f, w, h });
        }

        InventorySlot draggedSlot;
        Object* dragged = draggedItem(ui, view, &draggedSlot);
        if (view.container == nullptr && view.critter == gDude && inventoryCanUseOnSelf(dragged)) {
            muiStrokeRoundRect(figure.inset(-ui.dp(2.0f)), ui.dp(8.0f), ui.dp(2.0f), theme.accent);
            float iconSize = ui.dp(18.0f);
            float iconX = figure.x + iconSize * 0.9f;
            float iconY = figure.y + iconSize * 0.9f;
            muiFillCircle(iconX, iconY, iconSize * 0.8f, theme.buttonPrimary);
            muiDrawIcon(MuiIcon::Use, iconX, iconY, iconSize, ui.dp(1.5f), theme.buttonPrimaryText);
        }

        drawFlash(ui, figure, flash.target == FlashTarget::Figure);

        float armorSize = rect.right() - figure.right() - gap;
        MuiRect armor = { figure.right() + gap, rect.y, armorSize, armorSize };
        buildSlot(ui, view, InventorySlot::Armor, view.armor, armor, false);

        // Hit points, armor class, weight.
        InventorySummary summary;
        if (inventoryGetSummary(&summary)) {
            float lineHeight = ui.dp(22.0f);
            float iconSize = ui.dp(14.0f);
            float y = armor.bottom() + ui.dp(10.0f);
            struct Stat {
                MuiIcon icon;
                std::u32string value;
                MuiColor color;
            };
            const Stat stats[3] = {
                { MuiIcon::HitPoints, trimmed(summary.defense[0].value), theme.text },
                { MuiIcon::FilterArmor, trimmed(summary.defense[1].value), theme.text },
                { MuiIcon::Weight, number(view.weight) + U"/" + number(view.carryWeight), summary.encumbered ? kEncumbered : theme.text },
            };
            for (const Stat& stat : stats) {
                if (y + lineHeight > figure.bottom()) {
                    break;
                }
                muiDrawIcon(stat.icon, armor.x + iconSize / 2.0f, y + lineHeight / 2.0f, iconSize, ui.dp(1.5f), theme.accent);
                muiDrawTextAligned(stat.value, { armor.x + iconSize + ui.dp(6.0f), y, armor.w, lineHeight }, ui.dp(13.0f), stat.color, MuiAlign::Start, MuiAlign::Center);
                y += lineHeight;
            }
        }

        // Hands, the active one is marked.
        float handsY = figure.bottom() + gap;
        float handSize = std::min((rect.w - gap) / 2.0f, rect.bottom() - handsY);
        MuiRect leftHand = { rect.x, handsY, handSize, handSize };
        MuiRect rightHand = { rect.right() - handSize, handsY, handSize, handSize };
        buildSlot(ui, view, InventorySlot::LeftHand, view.leftHand, leftHand, view.activeHand == HAND_LEFT);
        buildSlot(ui, view, InventorySlot::RightHand, view.rightHand, rightHand, view.activeHand == HAND_RIGHT);
    }

    void InventoryScreen::buildSlot(MuiContext& ui, const InventoryView& view, InventorySlot slot, Object* item, const MuiRect& rect, bool active)
    {
        const MuiTheme& theme = muiTheme();
        int slotIndex = static_cast<int>(slot) - 1;
        slotRects[slotIndex] = rect;

        std::string id = slotIndex == 2 ? "inventory.slot.armor" : (slotIndex == 0 ? "inventory.slot.left" : "inventory.slot.right");

        // Can the dragged item go here?
        InventorySlot draggedSlot;
        Object* dragged = draggedItem(ui, view, &draggedSlot);
        bool target = false;
        if (dragged != nullptr && draggedSlot != slot) {
            target = slot == InventorySlot::Armor
                ? itemGetType(dragged) == ITEM_TYPE_ARMOR
                : true;
        }

        bool pressed = false;
        bool longPressed = false;
        bool tapped = false;
        bool isDragged = false;
        if (item != nullptr) {
            tapped = ui.touchable(id, rect, &pressed, &longPressed);
            isDragged = ui.dragSource(id, kPayloadSlotBase + static_cast<int>(slot), true);
        } else {
            ui.mark(id, rect);
        }

        if (isDragged || item == nullptr) {
            muiFillRoundRect(rect, ui.dp(5.0f), theme.button);
            muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(1.0f), theme.buttonBorder);
            MuiIcon icon = slot == InventorySlot::Armor ? MuiIcon::FilterArmor : MuiIcon::Use;
            muiDrawIcon(icon, rect.centerX(), rect.centerY(), rect.w * 0.34f, ui.dp(1.6f), theme.buttonBorder);
        } else {
            muiDrawItemCell(ui, rect, item, 1, pressed || item == selected);

            // Loaded ammo.
            int capacity = itemGetType(item) == ITEM_TYPE_WEAPON ? ammoGetCapacity(item) : 0;
            if (capacity > 0) {
                float barWidth = ui.dp(4.0f);
                MuiRect bar = { rect.right() - barWidth - ui.dp(5.0f), rect.y + ui.dp(6.0f), barWidth, rect.h - ui.dp(12.0f) };
                muiFillRoundRect(bar, ui.dp(2.0f), muiRgb(0x2A342C));
                float ratio = std::clamp(static_cast<float>(ammoGetQuantity(item)) / capacity, 0.0f, 1.0f);
                if (ratio > 0.0f) {
                    muiFillRoundRect({ bar.x, bar.bottom() - bar.h * ratio, bar.w, bar.h * ratio }, ui.dp(2.0f), theme.accent);
                }
            }
        }

        if (active) {
            muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(2.0f), theme.accent);
            muiFillCircle(rect.x + ui.dp(8.0f), rect.y + ui.dp(8.0f), ui.dp(3.0f), theme.accent);
        }

        drawFlash(ui, rect, (flash.target == FlashTarget::Slot && flash.slot == slot) || (flash.target == FlashTarget::Item && item != nullptr && flash.item == item));

        // Target: loading ammo or putting into a container in the hand is
        // shown like on list items.
        if (target) {
            bool onto = item != nullptr && inventoryCanDropOnto(dragged, item);
            muiStrokeRoundRect(rect.inset(-ui.dp(2.0f)), ui.dp(6.0f), ui.dp(2.0f), onto ? theme.accent : theme.buttonPrimary);
        }

        if (longPressed) {
            openActionMenu(ui, item, slot);
        } else if (tapped) {
            selected = selected == item ? nullptr : item;
        }
    }

    // Character summary as the game shows it, or the chosen item. While
    // dragging it's where items are dropped on the ground.
    void InventoryScreen::buildInfo(MuiContext& ui, const InventoryView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        infoRect = rect;

        if (ui.dragging(nullptr)) {
            muiFillRoundRect(rect, ui.dp(6.0f), kDropZone);
            muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(2.0f), kWarning);
            float iconSize = ui.dp(34.0f);
            muiDrawIcon(MuiIcon::Drop, rect.centerX(), rect.centerY() - iconSize * 0.4f, iconSize, ui.dp(2.2f), kWarning);
            MuiRect label = { rect.x, rect.centerY() + iconSize * 0.4f, rect.w, muiLineHeight(ui.dp(14.0f)) };
            muiDrawTextAligned(muiDecodeGameText(muiText(kTextDrop, "Drop")), label, ui.dp(14.0f), kWarning, MuiAlign::Center, MuiAlign::Center);
            return;
        }

        ui.panel(rect);
        ui.mark("inventory.info", rect);

        // Examining runs scripts, only when the item or its state changes.
        if (selected != nullptr) {
            std::string key = std::to_string(reinterpret_cast<uintptr_t>(selected))
                + ":" + std::to_string(itemGetType(selected) == ITEM_TYPE_WEAPON ? ammoGetQuantity(selected) : 0)
                + ":" + std::to_string(reinterpret_cast<uintptr_t>(view.leftHand))
                + ":" + std::to_string(reinterpret_cast<uintptr_t>(view.rightHand))
                + ":" + std::to_string(reinterpret_cast<uintptr_t>(view.armor));
            if (key != infoKey) {
                infoKey = key;
                inventoryGetItemInfo(selected, &info);
            }
        } else {
            infoKey.clear();
        }

        MuiRect content = rect.inset(ui.dp(12.0f), ui.dp(10.0f));

        // Measure, scroll, draw.
        float height = 0.0f;
        if (selected != nullptr) {
            buildItemInfo(ui, content, &height, false);
        } else {
            buildSummary(ui, content, &height, false);
        }

        std::string scrollId = selected != nullptr ? "inventory.info.item" : "inventory.info.summary";
        float offset = ui.scroll(scrollId, content, height);

        muiPushClip(content);
        float y = content.y - offset;
        if (selected != nullptr) {
            buildItemInfo(ui, content, &y, true);
        } else {
            buildSummary(ui, content, &y, true);
        }
        muiPopClip();
    }

    // Lines of [content] width from [*y]; measuring only adds heights.
    struct InfoWriter {
        const MuiRect& content;
        float* y;
        bool draw;

        void text(const std::u32string& line, float size, MuiColor color)
        {
            if (draw) {
                muiDrawText(line, content.x, *y, size, color);
            }
            *y += muiLineHeight(size);
        }

        void row(const std::u32string& label, const std::u32string& value, float size, MuiColor labelColor, MuiColor valueColor)
        {
            if (draw) {
                MuiRect rect = { content.x, *y, content.w, muiLineHeight(size) };
                muiDrawTextAligned(label, rect, size, labelColor, MuiAlign::Start, MuiAlign::Center);
                muiDrawTextAligned(value, rect, size, valueColor, MuiAlign::End, MuiAlign::Center);
            }
            *y += muiLineHeight(size);
        }

        void separator(float thickness, float gap, MuiColor color)
        {
            *y += gap;
            if (draw) {
                muiFillRect({ content.x, *y, content.w, thickness }, color);
            }
            *y += thickness + gap;
        }
    };

    void InventoryScreen::buildSummary(MuiContext& ui, const MuiRect& content, float* y, bool draw)
    {
        const MuiTheme& theme = muiTheme();
        InventorySummary summary;
        if (!inventoryGetSummary(&summary)) {
            return;
        }

        float size = ui.dp(13.0f);
        float start = *y;
        InfoWriter writer = { content, y, draw };
        writer.text(muiDecodeGameText(summary.name.c_str()), ui.dp(15.0f), theme.accent);
        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);

        // Primary stats in two columns.
        float columnGap = ui.dp(14.0f);
        float columnWidth = (content.w - columnGap) / 2.0f;
        for (int index = 0; index < 7; index += 2) {
            for (int column = 0; column < 2 && index + column < 7; column++) {
                const InventoryInfoRow& stat = summary.stats[index + column];
                MuiRect rect = { content.x + column * (columnWidth + columnGap), *y, columnWidth, muiLineHeight(size) };
                if (draw) {
                    muiDrawTextAligned(trimmed(stat.label), rect, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
                    muiDrawTextAligned(trimmed(stat.value), rect, size, theme.text, MuiAlign::End, MuiAlign::Center);
                }
            }
            *y += muiLineHeight(size);
        }

        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);
        for (const InventoryInfoRow& row : summary.defense) {
            writer.row(trimmed(row.label), trimmed(row.value), size, theme.textDim, theme.text);
        }

        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);
        for (const std::string* hand : { summary.hands[0], summary.hands[1] }) {
            for (int line = 0; line < 3; line++) {
                if (hand[line].empty()) {
                    continue;
                }
                for (const std::u32string& wrapped : muiWrapText(muiDecodeGameText(hand[line].c_str()), content.w, size)) {
                    writer.text(wrapped, size, line == 0 ? theme.text : theme.textDim);
                }
            }
            *y += ui.dp(4.0f);
        }

        writer.separator(ui.dp(1.0f), ui.dp(2.0f), theme.panelBorder);
        writer.text(muiDecodeGameText(summary.weight.c_str()), size, summary.encumbered ? kEncumbered : theme.text);

        if (!draw) {
            *y -= start;
        }
    }

    void InventoryScreen::buildItemInfo(MuiContext& ui, const MuiRect& content, float* y, bool draw)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(13.0f);
        float start = *y;
        InfoWriter writer = { content, y, draw };

        for (const std::u32string& line : muiWrapText(muiDecodeGameText(info.name.c_str()), content.w, ui.dp(15.0f))) {
            writer.text(line, ui.dp(15.0f), theme.accent);
        }
        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);

        for (const std::string& paragraph : info.text) {
            for (const std::u32string& line : muiWrapText(muiDecodeGameText(paragraph.c_str()), content.w, size)) {
                writer.text(line, size, theme.text);
            }
            *y += ui.dp(4.0f);
        }

        if (!info.rows.empty() || info.actionPoints >= 0) {
            writer.separator(ui.dp(1.0f), ui.dp(2.0f), theme.panelBorder);
        }

        for (const InventoryInfoRow& row : info.rows) {
            writer.row(trimmed(row.label), trimmed(row.value), size, theme.textDim, theme.text);
        }

        // Action points of the primary attack: lightning, as on the HUD.
        if (info.actionPoints >= 0) {
            float lineHeight = muiLineHeight(size);
            if (draw) {
                float iconSize = size * 0.9f;
                muiDrawIcon(MuiIcon::ActionPoints, content.x + iconSize / 2.0f, *y + lineHeight / 2.0f, iconSize, ui.dp(1.4f), theme.textDim);
                muiDrawTextAligned(number(info.actionPoints), { content.x, *y, content.w, lineHeight }, size, theme.text, MuiAlign::End, MuiAlign::Center);
            }
            *y += lineHeight;
        }

        if (!draw) {
            *y -= start;
        }
    }

    // Glow fading out around [rect].
    void InventoryScreen::drawFlash(MuiContext& ui, const MuiRect& rect, bool matches)
    {
        if (!matches || flash.target == FlashTarget::None) {
            return;
        }

        float progress = static_cast<float>(now - flash.time) / kFlashMs;
        if (progress >= 1.0f) {
            flash.target = FlashTarget::None;
            return;
        }

        const MuiTheme& theme = muiTheme();
        float strength = 1.0f - progress * progress;
        MuiColor inner = theme.accent.withAlpha(static_cast<Uint8>(220 * strength));
        MuiColor outer = theme.accent.withAlpha(static_cast<Uint8>(90 * strength));
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(2.5f), inner);
        muiStrokeRoundRect(rect.inset(-ui.dp(3.0f)), ui.dp(9.0f), ui.dp(3.0f), outer);
    }

    Object* InventoryScreen::draggedItem(MuiContext& ui, const InventoryView& view, InventorySlot* slot)
    {
        int payload;
        if (!ui.dragging(&payload)) {
            return nullptr;
        }

        if (payload >= kPayloadSlotBase) {
            *slot = static_cast<InventorySlot>(payload - kPayloadSlotBase);
            return slotItem(view, *slot);
        }

        *slot = InventorySlot::None;
        return payload < static_cast<int>(items.size()) ? items[payload].item : nullptr;
    }

    void InventoryScreen::openActionMenu(MuiContext& ui, Object* item, InventorySlot slot)
    {
        InventoryAction action;
        action.type = InventoryActionType::OpenActionMenu;
        action.item = item;
        action.slot = slot;
        muiToScreen(ui.pointerX(), ui.pointerY(), &(action.x), &(action.y));
        inventoryQueueAction(action);
    }

    void InventoryScreen::move(Object* item, InventorySlot slot, InventoryDropTarget target, Object* targetItem)
    {
        switch (target) {
        case InventoryDropTarget::LeftHand:
            startFlash(FlashTarget::Slot, InventorySlot::LeftHand, nullptr);
            break;
        case InventoryDropTarget::RightHand:
            startFlash(FlashTarget::Slot, InventorySlot::RightHand, nullptr);
            break;
        case InventoryDropTarget::Armor:
            startFlash(FlashTarget::Slot, InventorySlot::Armor, nullptr);
            break;
        case InventoryDropTarget::Backpack:
            // Loaded weapon, container, or the item back in the list.
            startFlash(FlashTarget::Item, InventorySlot::None, targetItem != nullptr ? targetItem : item);
            break;
        case InventoryDropTarget::Body:
            startFlash(FlashTarget::Header, InventorySlot::None, nullptr);
            break;
        }

        InventoryAction action;
        action.type = InventoryActionType::Move;
        action.item = item;
        action.slot = slot;
        action.target = target;
        action.targetItem = targetItem;
        inventoryQueueAction(action);
    }

    void InventoryScreen::handleDrop(MuiContext& ui, const InventoryView& view)
    {
        InventorySlot slot;
        Object* item = draggedItem(ui, view, &slot);
        if (item == nullptr) {
            return;
        }

        if (ui.dropped(infoRect, nullptr)) {
            InventoryAction action;
            action.type = InventoryActionType::Drop;
            action.item = item;
            action.slot = slot;
            inventoryQueueAction(action);
            return;
        }

        const InventoryDropTarget slotTargets[3] = {
            InventoryDropTarget::LeftHand,
            InventoryDropTarget::RightHand,
            InventoryDropTarget::Armor,
        };
        for (int index = 0; index < 3; index++) {
            InventorySlot targetSlot = static_cast<InventorySlot>(index + 1);
            if (targetSlot != slot && ui.dropped(slotRects[index], nullptr)) {
                move(item, slot, slotTargets[index], nullptr);
                return;
            }
        }

        if (view.container == nullptr && view.critter == gDude && inventoryCanUseOnSelf(item) && ui.dropped(figureRect, nullptr)) {
            InventoryAction action;
            action.type = InventoryActionType::UseOnSelf;
            action.item = item;
            action.slot = slot;
            inventoryQueueAction(action);
            return;
        }

        // Out of the open container.
        if (view.container != nullptr && slot == InventorySlot::None && ui.dropped(crumbRect, nullptr)) {
            move(item, slot, InventoryDropTarget::Body, nullptr);
            return;
        }

        // On another item (load ammo, put into a container), or back from a
        // slot.
        for (const Cell& cell : cells) {
            if (cell.item != item && ui.dropped(cell.rect, nullptr)) {
                if (slot != InventorySlot::None || inventoryCanDropOnto(item, cell.item)) {
                    move(item, slot, InventoryDropTarget::Backpack, cell.item);
                }
                return;
            }
        }

        if (slot != InventorySlot::None && ui.dropped(listRect, nullptr)) {
            move(item, slot, InventoryDropTarget::Backpack, nullptr);
        }
    }

    // Dragged item under the finger with its name and weight.
    void InventoryScreen::drawDragged(MuiContext& ui, const InventoryView& view)
    {
        const MuiTheme& theme = muiTheme();

        InventorySlot slot;
        Object* item = draggedItem(ui, view, &slot);
        if (item == nullptr) {
            return;
        }

        float size = ui.dp(64.0f);
        MuiRect cell = { ui.pointerX() - size / 2.0f, ui.pointerY() - size * 1.1f, size, size };
        muiDrawItemCell(ui, cell, item, 1, true);

        std::u32string text = muiDecodeGameText(objectGetName(item));
        float textSize = ui.dp(13.0f);
        float iconSize = ui.dp(13.0f);
        std::u32string weight = number(itemGetWeight(item));
        float width = muiTextWidth(text, textSize) + ui.dp(10.0f) + iconSize + ui.dp(4.0f) + muiTextWidth(weight, textSize) + ui.dp(20.0f);
        MuiRect bubble = { std::clamp(ui.pointerX() - width / 2.0f, 0.0f, ui.screenRect().w - width), cell.y - ui.dp(30.0f), width, ui.dp(26.0f) };
        muiFillRoundRect(bubble, ui.dp(6.0f), theme.panel);
        muiStrokeRoundRect(bubble, ui.dp(6.0f), ui.dp(1.0f), theme.accent);
        float x = bubble.x + ui.dp(10.0f);
        muiDrawTextAligned(text, { x, bubble.y, width, bubble.h }, textSize, theme.text, MuiAlign::Start, MuiAlign::Center);
        x += muiTextWidth(text, textSize) + ui.dp(10.0f);
        muiDrawIcon(MuiIcon::Weight, x + iconSize / 2.0f, bubble.centerY(), iconSize, ui.dp(1.3f), theme.textDim);
        x += iconSize + ui.dp(4.0f);
        muiDrawTextAligned(weight, { x, bubble.y, width, bubble.h }, textSize, theme.text, MuiAlign::Start, MuiAlign::Center);
    }

} // namespace

void muiInventoryInit()
{
    if (!muiIsEnabled() || gInventoryScreenPushed) {
        return;
    }

    muiPush(&gInventoryScreen);
    gInventoryScreenPushed = true;
}

} // namespace fallout
