#ifndef INTERFACE_H
#define INTERFACE_H

#include "art.h"
#include "combat_defs.h"
#include "db.h"
#include "inventory.h"
#include "obj_types.h"

namespace fallout {

#define INDICATOR_BOX_WIDTH 130
#define INDICATOR_BOX_HEIGHT 21

#define INTERFACE_BAR_WIDTH 640
#define INTERFACE_BAR_HEIGHT 100

// Minimum radiation amount to display RADIATED indicator.
#define RADATION_INDICATOR_THRESHOLD 65

// Minimum poison amount to display POISONED indicator.
#define POISON_INDICATOR_THRESHOLD 0

enum InterfaceItemAction : int {
    INTERFACE_ITEM_ACTION_DEFAULT = -1,
    INTERFACE_ITEM_ACTION_USE,
    INTERFACE_ITEM_ACTION_PRIMARY,
    INTERFACE_ITEM_ACTION_PRIMARY_AIMING,
    INTERFACE_ITEM_ACTION_SECONDARY,
    INTERFACE_ITEM_ACTION_SECONDARY_AIMING,
    INTERFACE_ITEM_ACTION_RELOAD,
    INTERFACE_ITEM_ACTION_COUNT,
};

inline InterfaceItemAction operator++(InterfaceItemAction& e, int)
{
    InterfaceItemAction result = e;
    e = static_cast<InterfaceItemAction>(static_cast<int>(e) + 1);
    return result;
}

extern int gInterfaceBarWindow;
extern bool gInterfaceBarMode;
extern int gInterfaceBarWidth;
extern bool gInterfaceBarIsCustom;
extern int gInterfaceBarContentOffset;

int interfaceInit();
void interfaceReset();
void interfaceFree();
int interfaceLoad(File* stream);
int interfaceSave(File* stream);
void interfaceBarHide();
void interfaceBarShow();
void interfaceBarEnable();
void interfaceBarDisable();
bool interfaceBarEnabled();
bool interfaceBarIsHidden();
// The bar's state exists (the game has made it; with the touch HUD it has no
// window).
bool interfaceBarExists();
// The end turn and end combat buttons are out (in combat).
bool interfaceBarEndButtonsVisible();
void interfaceBarRefresh();
void interfaceRenderHitPoints(bool animate);
void interfaceRenderArmorClass(bool animate);
void interfaceRenderActionPoints(int actionPointsLeft, int bonusActionPoints);
int interfaceGetCurrentHitMode(HitMode* hitMode, bool* aiming);

int interfaceUpdateItems(bool animated, InterfaceItemAction leftItemAction, InterfaceItemAction rightItemAction);
int interfaceBarSwapHands(bool animated);
int interfaceGetItemActions(InterfaceItemAction* leftItemAction, InterfaceItemAction* rightItemAction);
int interfaceCycleItemAction();

// CE: Item actions (attack modes, reload) of the active hand for the touch
// HUD, in `interfaceCycleItemAction` order. Empty for non weapons.
int interfaceGetAvailableItemActions(InterfaceItemAction* actions, int capacity);
InterfaceItemAction interfaceGetCurrentItemAction();
bool interfaceSetCurrentItemAction(InterfaceItemAction action);
// Text art of the item button for [action] (localized with game data).
InterfaceFrmId interfaceGetItemActionFrmId(InterfaceItemAction action, bool* aiming);

// What the item button of the active hand shows.
struct InterfaceItemButtonInfo {
    // Inventory art of the item, invalid for bare hands.
    FrmId itemFrmId;
    // Action text art (attack mode, reload, use), invalid for none.
    InterfaceFrmId textFrmId;
    bool aiming;
    // -1 when the action has no cost to show.
    int actionPoints;
    bool disabled;
};
bool interfaceGetItemButtonInfo(InterfaceItemButtonInfo* info);
bool interfaceGetCurrentItemAmmo(int* quantity, int* capacity);

// CE: Active indicator (SNEAK, LEVEL, ..., sfall custom tags) for the touch
// HUD, in the bar order.
struct InterfaceIndicator {
    char text[32];
    Color color;
};
void _intface_use_item();
Hand interfaceGetCurrentHand();
int interfaceGetActiveItem(Object** itemPtr);
int _intface_update_ammo_lights();
void interfaceBarEndButtonsShow(bool animated);
void interfaceBarEndButtonsHide(bool animated);
void interfaceBarEndButtonsRenderGreenLights();
void interfaceBarEndButtonsRenderRedLights();
int indicatorBarRefresh();
bool indicatorBarShow();
bool indicatorBarHide();
bool interface_get_current_attack_mode(HitMode* hitMode);
int interfaceTagAdd();
int interfaceTagGetMax();
bool interfaceTagShow(int tag);
bool interfaceTagHide(int tag);
bool interfaceTagIsActive(int tag);
void interfaceTagSetText(int tag, const char* text, int color);

unsigned char* customInterfaceBarGetBackgroundImageData();

} // namespace fallout

#endif /* INTERFACE_H */
