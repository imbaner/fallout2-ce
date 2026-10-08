#ifndef INVENTORY_H
#define INVENTORY_H

#include <string>
#include <vector>

#include "animation_defs.h"
#include "art.h"
#include "obj_types.h"
#include "proto_types.h"

namespace fallout {

enum class InvenSlot : int;

#define INVENTORY_SLOT_WIDTH 64
#define INVENTORY_SLOT_HEIGHT 48

// Extra slots per scroller added by the expanded barter/trade window.
constexpr int kExpandedBarterExtraSlots = 1;

enum Hand : int {
    // Item1 (Punch)
    HAND_LEFT,
    // Item2 (Kick)
    HAND_RIGHT,
    HAND_COUNT,
};

typedef void InventoryPrintItemDescriptionHandler(const char* string);

void inventoryResetDude();
void inventorySetDude(Object* obj, int pid);
void inventoryOpen();

int inventoryGetInvenApCost();

// Whether [critter]'s inventory opens now (`inventoryOpen`): in combat only
// on its turn and, for dude, for the action points it costs (0 out of
// combat, set in [actionPoints]).
enum class InventoryOpenCheck {
    Ok,
    NotYourTurn,
    NoActionPoints,
};

InventoryOpenCheck inventoryCheckOpen(Object* critter, int* actionPoints);

// The game's message when the inventory doesn't open for want of action
// points (to the message log).
void inventoryReportNoActionPoints();
void inventorySetInvenApCost(int cost);
void inventoryResetInvenApCost();
void adjustCritterStatsOnArmorChange(Object* critter, Object* oldArmor, Object* newArmor);
// The equipment the inventory screens hold aside and whose it is
// (`[debug] action_log` checks they're clear outside the screens).
struct InventoryHeldEquipment {
    Object* critter;
    Object* armor;
    Object* rightHand;
    Object* leftHand;
};

void inventoryGetHeldEquipment(InventoryHeldEquipment* held);

// CE: A party member whose items the loot and barter screens can reach now
// (`[qol] party_loot_and_barter`): the dude always, the others on his
// elevation within 30 tiles (Inventory Filter's range).
bool inventoryPartyMemberIsReachable(Object* critter);
FrmId inventoryComputeCritterFrmId(Object* critter, int basePid, Object* rightHandItem, Object* leftHandItem, Object* armor, Hand activeHand, AnimationType anim, Rotation rotation);
void inventoryOpenUseItemOn(Object* targetObj);
Object* critterGetItem2(Object* critter);
Object* critterGetItem1(Object* critter);
Object* critterGetArmor(Object* critter);

struct CritterEquipped {
    Object* leftHand = nullptr;
    Object* rightHand = nullptr;
    Object* armor = nullptr;
    int weight = 0;
};
CritterEquipped critterStripEquipped(Object* critter);
void critterRestoreEquipped(Object* critter, CritterEquipped& equipped);
Object* objectGetCarriedObjectByPid(Object* obj, int pid);
int objectGetCarriedQuantityByPid(Object* obj, int pid);
Object* inventoryFindByType(Object* obj, ItemType itemType, int* indexPtr);
Object* inventoryFindById(Object* obj, int id);
Object* inventoryItemByIndex(Object* obj, int index);
// Makes critter equip a given item in a given hand slot with an animation.
// 0 - left hand, 1 - right hand. If item is armor, hand value is ignored.
int inventoryEquip(Object* critter, Object* item, Hand hand);
// Same as inven_wield but allows to wield item without animation.
int inventoryEquipFunc(Object* critter, Object* item, Hand hand, bool animate);
// Makes critter unequip an item in a given hand slot with an animation.
int inventoryUnequip(Object* critter, Hand hand);
// Same as inven_unwield but allows to unwield item without animation.
int inventoryUnequipFunc(Object* critter, Hand hand, bool animate);
int inventoryOpenLooting(Object* looter, Object* target);
int inventoryOpenStealing(Object* thief, Object* target);
void barterProcessUI(int win, Object* barterer, Object* playerTable, Object* bartererTable, int barterMod);
int inventorySetTimer(Object* item);
int inventoryGetWindow();
void inventoryDisplayStats();
void inventoryRedraw(int redrawSide);
Object* inventoryGetTargetObject();
int inventoryUnwieldSlot(Object* critter, InvenSlot slot);

// CE: Barter screen of the mobile UI (mui_barter.cc). The barter
// (`barterProcessUI`) runs without the game's windows then: its loop performs
// the screen's actions and buttons (it checks them every frame) with the
// functions the game's window loop runs.

struct BarterView {
    // Player side: player or a party member (`[qol] party_loot_and_barter`).
    Object* dude;
    Object* playerTable;
    Object* barterer;
    Object* bartererTable;
    // Barter with a party member compares weights instead of prices.
    bool partyMemberBarter;
    int requestValue;
    int offerValue;
    std::vector<Object*> party;
    int partyIndex;
};

enum class BarterActionType {
    // Item of a side's inventory to its table and back.
    MoveToTable,
    MoveFromTable,
    // Item of the player side to party member `partyIndex`.
    GiveToPartyMember,
    // Show party member `partyIndex` on the player side.
    SelectPartyMember,
    // Game's action menu (look, use, unload) for the item of a side's
    // inventory or table (`onTable`) at screen point (`x`, `y`).
    OpenActionMenu,
};

struct BarterAction {
    BarterActionType type;
    bool playerSide;
    Object* item;
    int partyIndex = -1;
    bool onTable = false;
    int x = 0;
    int y = 0;
};

// The barter loop's buttons.
enum class BarterRequest {
    None,
    // Offer (the game's M).
    Offer,
    // Back to talk, tables go back (the game's T).
    Talk,
};

bool barterGetView(BarterView* view);
void barterQueueAction(const BarterAction& action);
void barterRequest(BarterRequest request);
// What [item] is worth in this deal: player's items at their cost, trader's
// ones with the trader's markup (the same formula as the table totals).
int barterGetItemPrice(Object* item, bool playerSide);

// CE: Inventory screen of the mobile UI (mui_inventory.cc). The inventory
// (`inventoryOpen`) runs without the game's window then: its loop performs
// the screen's actions (it checks them every frame) with the same functions
// as mouse dragging and the action menu.

enum class InventorySlot {
    // Listed item (inventory or open container).
    None,
    LeftHand,
    RightHand,
    Armor,
};

enum class InventoryDropTarget {
    // Item list, `targetItem` - item it was dropped on (container, weapon for
    // ammo) or nullptr.
    Backpack,
    LeftHand,
    RightHand,
    Armor,
    // Character view; inside a container takes the item out of it.
    Body,
};

struct InventoryView {
    // Whose inventory it is.
    Object* critter;
    // Open container (bag, backpack), nullptr for the inventory itself.
    Object* container;
    // Owner of the listed items (the critter or the open container).
    Object* listOwner;
    Object* leftHand;
    Object* rightHand;
    Object* armor;
    Hand activeHand;
    // Total weight (with equipped items) and carry weight of the critter.
    int weight;
    int carryWeight;
    // Character view as the game draws it.
    FrmId bodyFrmId;
    int bodyRotation;
    // Last item looked at with the action menu.
    Object* examinedItem;
    unsigned int examinedVersion;
    // Last action chosen in the action menu (`GAME_MOUSE_ACTION_MENU_ITEM_*`)
    // and its item.
    int lastMenuAction;
    Object* lastMenuItem;
    unsigned int lastMenuVersion;
};

enum class InventoryActionType {
    // `item` from `slot` to `target`.
    Move,
    // Consume a drug/food item on the player through the game's Use path.
    UseOnSelf,
    // Game's action menu (look, use, unload, drop) for `item` at screen
    // point (`x`, `y`).
    OpenActionMenu,
    // `item` from `slot` to the ground (asks quantity).
    Drop,
    // All `items` of the list to the ground.
    DropAll,
    CloseContainer,
};

struct InventoryAction {
    InventoryActionType type;
    Object* item = nullptr;
    InventorySlot slot = InventorySlot::None;
    InventoryDropTarget target = InventoryDropTarget::Backpack;
    Object* targetItem = nullptr;
    int x = 0;
    int y = 0;
    std::vector<Object*> items;
};

bool inventoryGetView(InventoryView* view);
void inventoryQueueAction(const InventoryAction& action);
// The inventory loop closes (the game's Esc).
void inventoryRequestClose();
// Turns the character view (the screen turns it by swipes).
void inventorySetBodyRotation(int rotation);
// Dropping [item] on [target] does something (puts it into a container,
// loads ammo into a weapon).
bool inventoryCanDropOnto(Object* item, Object* target);
// Consumables that the player can take by dropping them on the figure.
bool inventoryCanUseOnSelf(Object* item);

// CE: Loot screen of the mobile UI (mui_loot.cc): looting and stealing
// (`inventoryOpenLooting`) run without the game's window, the same way as the
// inventory.

struct LootView {
    // Left side: the player or a party member (`[qol]
    // party_loot_and_barter`), open container on that side.
    Object* looter;
    Object* leftOwner;
    Object* leftContainer;
    // Right side: what is looted, open container in it.
    Object* target;
    Object* rightOwner;
    Object* rightContainer;
    bool steal;
    // Party members as tabs; stealing from a party member switches the
    // right side between them instead.
    std::vector<Object*> party;
    int partyIndex;
    bool partySwitchesTarget;
    // Dead critters lying at the same tile.
    int targetCount;
    int targetIndex;
    int weight;
    int carryWeight;
};

enum class LootActionType {
    // `item` to the other side (from the right one with `fromTarget`).
    Move,
    // Game's action menu for `item` at screen point (`x`, `y`).
    OpenActionMenu,
    // All `items` of the right side to the left one ("Take all").
    TakeAll,
    // All `items` of the left side to the right one (Inventory Filter's
    // "Give all").
    GiveAll,
    // Player side `item` to party member `index`.
    GiveToPartyMember,
    SelectPartyMember,
    // Next (`index` 1) or previous (-1) dead critter at the tile.
    SwitchTarget,
    // Open container of the right side (`fromTarget`) or the left one.
    CloseContainer,
};

struct LootAction {
    LootActionType type;
    Object* item = nullptr;
    bool fromTarget = false;
    int index = 0;
    int x = 0;
    int y = 0;
    std::vector<Object*> items;
};

bool lootGetView(LootView* view);
void lootQueueAction(const LootAction& action);
// The loot loop closes (the game's Esc).
void lootRequestClose();

struct InventoryInfoRow {
    std::string label;
    std::string value;
};

// Character summary of the inventory window (texts as the game shows them).
struct InventorySummary {
    std::string name;
    // Primary stats.
    InventoryInfoRow stats[7];
    // Hit points, armor class, damage thresholds/resistances.
    InventoryInfoRow defense[7];
    // Item name (or "No item"), damage/range, ammo of each hand.
    std::string hands[2][3];
    std::string weight;
    bool encumbered;
};

bool inventoryGetSummary(InventorySummary* summary);

// Item description as looking at it shows (name, text, weight), weapon damage,
// range and AP, armor class and damage thresholds/resistances.
struct InventoryItemInfo {
    std::string name;
    std::vector<std::string> text;
    std::vector<InventoryInfoRow> rows;
    // Weapon's primary attack, -1 for others.
    int actionPoints;
};

void inventoryGetItemInfo(Object* item, InventoryItemInfo* info);

} // namespace fallout

#endif /* INVENTORY_H */
