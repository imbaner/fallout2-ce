#include "mui_screens.h"

#include <algorithm>
#include <string>
#include <vector>

#include "game.h"
#include "game_dialog.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "kb.h"
#include "message.h"
#include "object.h"
#include "stat.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg`, English fallbacks for data without them.
    constexpr int kTextOffer = 117;
    constexpr int kTextYourOffer = 118;
    constexpr int kTextRequest = 119;
    constexpr int kTextWeight = 120;

    const MuiColor kBackground = muiRgb(0x060A07);

    // Item lists of the barter screen.
    enum List {
        LIST_PLAYER,
        LIST_PLAYER_TABLE,
        LIST_BARTERER_TABLE,
        LIST_BARTERER,
        LIST_COUNT,
    };

    const char* const kListIds[LIST_COUNT] = {
        "barter.player",
        "barter.offer",
        "barter.request",
        "barter.barterer",
    };

    // Drag payload: list and index in it.
    constexpr int kPayloadListStride = 100000;

    // Barter screen: player side (party tabs, filters, three columns), offer
    // and request tables (a column each), trader side (filters, three
    // columns). All item cells are squares of one size.
    // Tap moves an item to the table and back, dragging does the same and
    // gives items to party members over their tabs. Works over the game's
    // barter loop (see `barterQueueAction`).
    class BarterScreen : public MuiScreen {
    public:
        MuiItemFilterState playerFilter;
        MuiItemFilterState bartererFilter;
        bool wasActive = false;

        std::vector<MuiListItem> lists[LIST_COUNT];
        MuiRect listRects[LIST_COUNT];
        std::vector<MuiRect> partyTabRects;
        float cellSize = 0.0f;

        MessageList messageList;
        bool messageListLoaded = false;

        BarterScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return true; }

        bool isActive() override
        {
            BarterView view;
            bool active = muiIsEnabled() && barterGetView(&view);
            if (active && !wasActive) {
                // New barter.
                playerFilter = MuiItemFilterState();
                bartererFilter = MuiItemFilterState();
            }
            wasActive = active;
            return active;
        }

        void build(MuiContext& ui) override;
        // Back to talk (the talk tab).
        void back() override { barterRequest(BarterRequest::Talk); }

        const char* text(int id, const char* fallback)
        {
            if (messageListLoaded) {
                MessageListItem item;
                item.num = id;
                if (messageListGetItem(&messageList, &item)) {
                    return item.text;
                }
            }
            return fallback;
        }

    private:
        void buildList(MuiContext& ui, int list, int columns, const MuiRect& rect);
        void buildPartyTabs(MuiContext& ui, const BarterView& view, const MuiRect& rect);
        void handleTap(int list, Object* item);
        void openActionMenu(MuiContext& ui, int list, Object* item);
        void handleDrop(MuiContext& ui, const BarterView& view);
        void drawDragged(MuiContext& ui, const BarterView& view);
    };

    BarterScreen* gBarterScreen = nullptr;

    void queue(BarterActionType type, bool playerSide, Object* item, int partyIndex = -1)
    {
        barterQueueAction({ type, playerSide, item, partyIndex });
    }

    std::u32string number(int value)
    {
        return muiDecodeUtf8(std::to_string(value).c_str());
    }

    void BarterScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();

        BarterView view;
        if (!barterGetView(&view)) {
            return;
        }

        // Opaque over the map.
        muiFillRect(ui.screenRect(), kBackground);

        MuiRect content;
        MuiRect tabs;
        muiDialogLayout(ui, &content, &tabs);

        int tab = muiDialogTabs(ui, tabs, MuiDialogTab::Barter, true, gameDialogSpeakerIsPartyMember());
        if (tab == static_cast<int>(MuiDialogTab::Talk)) {
            // Back to talk, tables go back like with the game's talk button.
            barterRequest(BarterRequest::Talk);
        } else if (tab == static_cast<int>(MuiDialogTab::Review)) {
            muiGameDialogOpenReview();
            barterRequest(BarterRequest::Talk);
        } else if (tab == static_cast<int>(MuiDialogTab::Party)) {
            muiGameDialogOpenParty();
            barterRequest(BarterRequest::Talk);
        }

        // Item columns: player 3, offer 1, request 1, trader 3. A list is its
        // cells with gaps between them and around.
        constexpr int kSideColumns = 3;
        float cellGap = ui.dp(4.0f);
        float groupGap = ui.dp(8.0f);
        int listGaps = (kSideColumns + 1) * 2 + 2 * 2;
        cellSize = (content.w - groupGap * 3.0f - cellGap * listGaps) / (kSideColumns * 2 + 2);
        float sideWidth = cellSize * kSideColumns + cellGap * (kSideColumns + 1);
        float unit = cellSize + cellGap * 2.0f;
        float playerX = content.x;
        float offerX = playerX + sideWidth + groupGap;
        float requestX = offerX + unit + groupGap;
        float bartererX = requestX + unit + groupGap;

        // Sides: lists down to their totals at the bottom; tables: lists,
        // totals, the offer button under both.
        float rowHeight = ui.dp(30.0f);
        float gap = ui.dp(6.0f);
        float buttonHeight = ui.dp(40.0f);
        float infoHeight = ui.dp(18.0f);
        float headerY = content.y;
        float filterY = headerY + rowHeight + gap;
        float listY = filterY + rowHeight + gap;
        float sideInfoY = content.bottom() - infoHeight;
        float sideListHeight = sideInfoY - gap - listY;
        float buttonY = content.bottom() - buttonHeight;
        float tableInfoY = buttonY - gap - infoHeight;
        float tableListHeight = tableInfoY - gap - listY;

        // Headers: party tabs (or player name), trader name and money.
        buildPartyTabs(ui, view, { playerX, headerY, sideWidth, rowHeight });

        float nameSize = ui.dp(14.0f);
        std::u32string bartererName = muiDecodeGameText(objectGetName(view.barterer));
        if (!view.partyMemberBarter) {
            bartererName += U"  $" + number(itemGetTotalCaps(view.barterer));
        }
        muiDrawTextAligned(bartererName, { bartererX, headerY, sideWidth, rowHeight }, nameSize, theme.accent, MuiAlign::Start, MuiAlign::Center);

        // Filters and table titles.
        muiItemFilterRow(ui, "barter.filter.player", { playerX, filterY, sideWidth, rowHeight }, &playerFilter);
        muiItemFilterRow(ui, "barter.filter.barterer", { bartererX, filterY, sideWidth, rowHeight }, &bartererFilter);
        float titleSize = ui.dp(12.0f);
        muiDrawTextAligned(muiDecodeGameText(text(kTextYourOffer, "OFFER")), { offerX, filterY, unit, rowHeight }, titleSize, theme.textDim, MuiAlign::Center, MuiAlign::Center);
        muiDrawTextAligned(muiDecodeGameText(text(kTextRequest, "REQUEST")), { requestX, filterY, unit, rowHeight }, titleSize, theme.textDim, MuiAlign::Center, MuiAlign::Center);

        // Lists.
        MuiItemFilterState all;
        lists[LIST_PLAYER] = muiInventoryItems(view.dude, playerFilter);
        lists[LIST_PLAYER_TABLE] = muiInventoryItems(view.playerTable, all);
        lists[LIST_BARTERER_TABLE] = muiInventoryItems(view.bartererTable, all);
        lists[LIST_BARTERER] = muiInventoryItems(view.barterer, bartererFilter);

        buildList(ui, LIST_PLAYER, kSideColumns, { playerX, listY, sideWidth, sideListHeight });
        buildList(ui, LIST_PLAYER_TABLE, 1, { offerX, listY, unit, tableListHeight });
        buildList(ui, LIST_BARTERER_TABLE, 1, { requestX, listY, unit, tableListHeight });
        buildList(ui, LIST_BARTERER, kSideColumns, { bartererX, listY, sideWidth, sideListHeight });

        // Totals: player's weight and money (and of the shown category), table
        // prices (weights when bartering with a party member).
        float infoSize = ui.dp(12.0f);
        std::u32string weightLabel = muiDecodeGameText(text(kTextWeight, "Wt."));
        int weight = objectGetInventoryWeight(view.dude);
        int carry = critterGetStat(view.dude, STAT_CARRY_WEIGHT);
        std::u32string playerInfo = weightLabel + U" " + number(weight) + U"/" + number(carry) + U"  $" + number(itemGetTotalCaps(view.dude));
        if (playerFilter.filter != MuiItemFilter::All) {
            playerInfo += U"  (" + number(muiItemsWeight(lists[LIST_PLAYER])) + U")";
        }
        muiDrawTextAligned(playerInfo, { playerX, sideInfoY, sideWidth, infoHeight }, infoSize, theme.textDim, MuiAlign::Start, MuiAlign::Center);

        std::u32string prefix = view.partyMemberBarter ? weightLabel + U" " : U"$";
        muiDrawTextAligned(prefix + number(view.offerValue), { offerX, tableInfoY, unit, infoHeight }, ui.dp(13.0f), theme.text, MuiAlign::Center, MuiAlign::Center);
        muiDrawTextAligned(prefix + number(view.requestValue), { requestX, tableInfoY, unit, infoHeight }, ui.dp(13.0f), theme.text, MuiAlign::Center, MuiAlign::Center);

        // The trader's shown items' weight, every filter ("All" too).
        muiDrawTextAligned(weightLabel + U" " + number(muiItemsWeight(lists[LIST_BARTERER])), { bartererX, sideInfoY, sideWidth, infoHeight }, infoSize, theme.textDim, MuiAlign::Start, MuiAlign::Center);

        // Offer under both tables.
        MuiRect offerRect = { offerX, buttonY, requestX + unit - offerX, buttonHeight };
        if (ui.button("barter.offer.button", offerRect, muiDecodeGameText(text(kTextOffer, "OFFER")), MuiButtonStyle::Primary)) {
            barterRequest(BarterRequest::Offer);
        }

        handleDrop(ui, view);
        drawDragged(ui, view);
    }

    void BarterScreen::buildPartyTabs(MuiContext& ui, const BarterView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        partyTabRects.clear();

        // Only the player: just the name.
        if (view.party.size() <= 1) {
            muiDrawTextAligned(muiDecodeGameText(objectGetName(view.dude)), rect, ui.dp(14.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
            return;
        }

        int dragPayload;
        bool dragFromPlayer = ui.dragging(&dragPayload) && dragPayload / kPayloadListStride == LIST_PLAYER;

        float gap = ui.dp(3.0f);
        int count = static_cast<int>(view.party.size());
        float width = (rect.w - gap * (count - 1)) / count;
        for (int index = 0; index < count; index++) {
            MuiRect tab = { rect.x + index * (width + gap), rect.y, width, rect.h };
            partyTabRects.push_back(tab);

            bool active = index == view.partyIndex;
            bool pressed;
            bool tapped = ui.touchable("barter.party." + std::to_string(index), tab, &pressed);

            // Tabs of other members are drop targets for player's items.
            bool reachable = inventoryPartyMemberIsReachable(view.party[index]);
            bool target = dragFromPlayer && !active && reachable;
            MuiColor fill = pressed ? theme.buttonPressed : (active ? theme.buttonPrimary : theme.button);
            MuiColor color = active && !pressed ? theme.buttonPrimaryText : theme.accent;
            muiFillRoundRect(tab, ui.dp(5.0f), fill);
            muiStrokeRoundRect(tab, ui.dp(5.0f), ui.dp(target ? 2.0f : 1.0f), target || active ? theme.accent : theme.buttonBorder);

            MuiRect label = tab.inset(ui.dp(4.0f), 0.0f);
            muiPushClip(label);
            muiDrawTextAligned(muiDecodeGameText(objectGetName(view.party[index])), label, ui.dp(12.0f), color, MuiAlign::Center, MuiAlign::Center);
            muiPopClip();
            if (!reachable) {
                muiDrawUnavailableOverlay(ui, tab, 5.0f);
            }

            if (tapped && !active) {
                if (reachable) {
                    queue(BarterActionType::SelectPartyMember, true, nullptr, index);
                } else {
                    muiNotifyPartyMemberOutOfReach(view.party[index]);
                }
            }
        }
    }

    void BarterScreen::buildList(MuiContext& ui, int list, int columns, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        listRects[list] = rect;

        // Table columns are drop targets while dragging from their side.
        int dragPayload;
        bool dragging = ui.dragging(&dragPayload);
        int dragList = dragging ? dragPayload / kPayloadListStride : -1;
        bool target = (list == LIST_PLAYER_TABLE && dragList == LIST_PLAYER)
            || (list == LIST_PLAYER && dragList == LIST_PLAYER_TABLE)
            || (list == LIST_BARTERER_TABLE && dragList == LIST_BARTERER)
            || (list == LIST_BARTERER && dragList == LIST_BARTERER_TABLE);

        muiFillRoundRect(rect, ui.dp(6.0f), muiRgb(0x0B120D));
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(target ? 2.0f : 1.0f), target ? theme.accent : theme.panelBorder);

        float gap = ui.dp(4.0f);
        MuiRect area = rect.inset(gap);
        float cell = cellSize;
        float cellHeight = cellSize;
        const std::vector<MuiListItem>& items = lists[list];
        int rows = (static_cast<int>(items.size()) + columns - 1) / columns;
        float contentHeight = rows * (cellHeight + gap);

        std::string id = kListIds[list];
        float offset = ui.scroll(id, area, contentHeight);
        bool scrolling = ui.isScrolling(id);
        // Vertical moves scroll only a list that doesn't fit.
        bool scrollable = contentHeight > area.h;

        muiPushClip(area);
        for (int index = 0; index < static_cast<int>(items.size()); index++) {
            int row = index / columns;
            int column = index % columns;
            MuiRect cellRect = { area.x + column * (cell + gap), area.y - offset + row * (cellHeight + gap), cell, cellHeight };
            if (cellRect.bottom() < area.y || cellRect.y > area.bottom()) {
                continue;
            }

            MuiRect touchRect = cellRect;
            touchRect.y = std::max(cellRect.y, area.y);
            touchRect.h = std::min(cellRect.bottom(), area.bottom()) - touchRect.y;

            std::string cellId = id + "." + std::to_string(index);
            ui.mark(id + ".pid." + std::to_string(items[index].item->pid), touchRect);
            bool pressed;
            bool longPressed;
            bool tapped = ui.touchable(cellId, touchRect, &pressed, &longPressed);
            if (longPressed && !scrolling) {
                openActionMenu(ui, list, items[index].item);
            }
            bool dragged = ui.dragSource(cellId, list * kPayloadListStride + index, !scrollable);

            if (dragged) {
                // Placeholder, the item follows the finger.
                muiStrokeRoundRect(cellRect, ui.dp(5.0f), ui.dp(1.0f), theme.buttonBorder);
                continue;
            }

            muiDrawItemCell(ui, cellRect, items[index].item, items[index].quantity, pressed);

            if (tapped && !scrolling) {
                handleTap(list, items[index].item);
            }
        }
        muiPopClip();
    }

    // Long press: the game's action menu (look, use, unload) around the
    // finger, like on the map.
    void BarterScreen::openActionMenu(MuiContext& ui, int list, Object* item)
    {
        BarterAction action;
        action.type = BarterActionType::OpenActionMenu;
        action.playerSide = list == LIST_PLAYER || list == LIST_PLAYER_TABLE;
        action.item = item;
        action.onTable = list == LIST_PLAYER_TABLE || list == LIST_BARTERER_TABLE;
        muiToScreen(ui.pointerX(), ui.pointerY(), &(action.x), &(action.y));
        barterQueueAction(action);
    }

    void BarterScreen::handleTap(int list, Object* item)
    {
        switch (list) {
        case LIST_PLAYER:
            queue(BarterActionType::MoveToTable, true, item);
            break;
        case LIST_PLAYER_TABLE:
            queue(BarterActionType::MoveFromTable, true, item);
            break;
        case LIST_BARTERER_TABLE:
            queue(BarterActionType::MoveFromTable, false, item);
            break;
        case LIST_BARTERER:
            queue(BarterActionType::MoveToTable, false, item);
            break;
        }
    }

    void BarterScreen::handleDrop(MuiContext& ui, const BarterView& view)
    {
        int payload;
        if (!ui.dragging(&payload)) {
            return;
        }

        int list = payload / kPayloadListStride;
        int index = payload % kPayloadListStride;
        if (list < 0 || list >= LIST_COUNT || index >= static_cast<int>(lists[list].size())) {
            return;
        }
        Object* item = lists[list][index].item;

        // Player's item over another party member's tab: give it.
        if (list == LIST_PLAYER) {
            for (int tab = 0; tab < static_cast<int>(partyTabRects.size()); tab++) {
                if (tab != view.partyIndex && ui.dropped(partyTabRects[tab], nullptr)) {
                    if (inventoryPartyMemberIsReachable(view.party[tab])) {
                        queue(BarterActionType::GiveToPartyMember, true, item, tab);
                    } else {
                        muiNotifyPartyMemberOutOfReach(view.party[tab]);
                    }
                    return;
                }
            }
        }

        int target = -1;
        switch (list) {
        case LIST_PLAYER:
            target = LIST_PLAYER_TABLE;
            break;
        case LIST_PLAYER_TABLE:
            target = LIST_PLAYER;
            break;
        case LIST_BARTERER_TABLE:
            target = LIST_BARTERER;
            break;
        case LIST_BARTERER:
            target = LIST_BARTERER_TABLE;
            break;
        }

        if (target != -1 && ui.dropped(listRects[target], nullptr)) {
            handleTap(list, item);
        }
    }

    // Dragged item under the finger with its name, weight and price in this
    // deal (weight only when bartering with a party member).
    void BarterScreen::drawDragged(MuiContext& ui, const BarterView& view)
    {
        const MuiTheme& theme = muiTheme();

        int payload;
        if (!ui.dragging(&payload)) {
            return;
        }

        int list = payload / kPayloadListStride;
        int index = payload % kPayloadListStride;
        if (list < 0 || list >= LIST_COUNT || index >= static_cast<int>(lists[list].size())) {
            return;
        }

        const MuiListItem& entry = lists[list][index];
        float size = cellSize;
        MuiRect cell = { ui.pointerX() - size / 2.0f, ui.pointerY() - size * 1.1f, size, size };
        muiDrawItemCell(ui, cell, entry.item, entry.quantity, true);

        std::u32string info = muiDecodeGameText(objectGetName(entry.item));
        info += U"  " + muiDecodeGameText(text(kTextWeight, "Wt.")) + U" " + number(itemGetWeight(entry.item));
        if (!view.partyMemberBarter) {
            bool playerSide = list == LIST_PLAYER || list == LIST_PLAYER_TABLE;
            info += U"  $" + number(barterGetItemPrice(entry.item, playerSide));
        }
        float textSize = ui.dp(13.0f);
        float width = muiTextWidth(info, textSize) + ui.dp(16.0f);
        MuiRect bubble = { std::clamp(ui.pointerX() - width / 2.0f, 0.0f, ui.screenRect().w - width), cell.y - ui.dp(30.0f), width, ui.dp(26.0f) };
        muiFillRoundRect(bubble, ui.dp(6.0f), theme.panel);
        muiStrokeRoundRect(bubble, ui.dp(6.0f), ui.dp(1.0f), theme.accent);
        muiDrawTextAligned(info, bubble, textSize, theme.text, MuiAlign::Center, MuiAlign::Center);
    }

} // namespace

void muiBarterInit()
{
    if (!muiIsEnabled() || gBarterScreen != nullptr) {
        return;
    }

    gBarterScreen = new BarterScreen();
    if (messageListInit(&(gBarterScreen->messageList))) {
        gBarterScreen->messageListLoaded = messageListLoad(&(gBarterScreen->messageList), "game\\ce.msg");
    }
    muiPush(gBarterScreen);
}

void muiBarterExit()
{
    if (gBarterScreen == nullptr) {
        return;
    }

    muiRemove(gBarterScreen);
    if (gBarterScreen->messageListLoaded) {
        messageListFree(&(gBarterScreen->messageList));
    }
    delete gBarterScreen;
    gBarterScreen = nullptr;
}

} // namespace fallout
