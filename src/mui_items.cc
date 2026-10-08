#include "mui_screens.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "art.h"
#include "critter.h"
#include "item.h"
#include "mui_icons.h"
#include "mui_notify.h"
#include "proto_types.h"

namespace fallout {

namespace {

    // Indexed by `MuiItemFilter`.
    const MuiIcon kFilterIcons[static_cast<int>(MuiItemFilter::Count)] = {
        MuiIcon::FilterAll,
        MuiIcon::FilterWeapons,
        MuiIcon::FilterWeaponsAndAmmo,
        MuiIcon::FilterArmor,
        MuiIcon::FilterDrugs,
        MuiIcon::FilterAmmo,
        MuiIcon::FilterMisc,
    };

} // namespace

bool muiItemMatchesFilter(Object* item, const MuiItemFilterState& state)
{
    int type = itemGetType(item);
    switch (state.filter) {
    case MuiItemFilter::All:
        return true;
    case MuiItemFilter::Weapons:
        if (type != ITEM_TYPE_WEAPON) {
            return false;
        }
        // Firearms and energy weapons use ammo.
        return !state.gunsOnly || ammoGetCapacity(item) > 0;
    case MuiItemFilter::WeaponsAndAmmo:
        return type == ITEM_TYPE_WEAPON || type == ITEM_TYPE_AMMO;
    case MuiItemFilter::Armor:
        return type == ITEM_TYPE_ARMOR;
    case MuiItemFilter::Drugs:
        return type == ITEM_TYPE_DRUG;
    case MuiItemFilter::Ammo:
        return type == ITEM_TYPE_AMMO;
    case MuiItemFilter::Misc:
        return type == ITEM_TYPE_MISC || type == ITEM_TYPE_CONTAINER || type == ITEM_TYPE_KEY;
    default:
        return true;
    }
}

void muiItemFilterRow(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiItemFilterState* state)
{
    const MuiTheme& theme = muiTheme();
    int count = static_cast<int>(MuiItemFilter::Count);
    float gap = ui.dp(3.0f);
    float width = (rect.w - gap * (count - 1)) / count;

    for (int index = 0; index < count; index++) {
        MuiRect button = { rect.x + index * (width + gap), rect.y, width, rect.h };
        MuiItemFilter filter = static_cast<MuiItemFilter>(index);

        bool pressed;
        bool longPressed;
        bool tapped = ui.touchable(id + "." + std::to_string(index), button, &pressed, &longPressed);

        if (tapped) {
            state->filter = filter;
            state->gunsOnly = false;
        } else if (longPressed && filter == MuiItemFilter::Weapons) {
            state->filter = filter;
            state->gunsOnly = !state->gunsOnly;
        }

        bool active = state->filter == filter;
        MuiColor fill = pressed ? theme.buttonPressed : (active ? theme.buttonPrimary : theme.button);
        MuiColor color = active && !pressed ? theme.buttonPrimaryText : theme.accent;
        muiFillRoundRect(button, ui.dp(5.0f), fill);
        muiStrokeRoundRect(button, ui.dp(5.0f), ui.dp(1.0f), active ? theme.accent : theme.buttonBorder);

        float size = std::min(button.w, button.h) * 0.52f;
        muiDrawIcon(kFilterIcons[index], button.centerX(), button.centerY(), size, ui.dp(1.6f), color);

        // Firearms only: a dot in the corner.
        if (active && filter == MuiItemFilter::Weapons && state->gunsOnly) {
            muiFillCircle(button.right() - ui.dp(5.0f), button.y + ui.dp(5.0f), ui.dp(2.5f), color);
        }
    }
}

std::vector<MuiListItem> muiInventoryItems(Object* owner, const MuiItemFilterState& state)
{
    std::vector<MuiListItem> items;
    if (owner == nullptr) {
        return items;
    }

    // The game lists the last added item first.
    Inventory* inventory = &(owner->data.inventory);
    for (int index = inventory->length - 1; index >= 0; index--) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        if (muiItemMatchesFilter(inventoryItem->item, state)) {
            items.push_back({ inventoryItem->item, inventoryItem->quantity });
        }
    }

    // Money first.
    std::stable_partition(items.begin(), items.end(), [](const MuiListItem& entry) {
        return ProtoId(entry.item) == ItemProtoTypeId::Money;
    });

    return items;
}

int muiItemsWeight(const std::vector<MuiListItem>& items)
{
    int weight = 0;
    for (const MuiListItem& entry : items) {
        weight += itemGetWeight(entry.item) * entry.quantity;
    }
    return weight;
}

void muiNotifyPartyMemberOutOfReach(Object* critter)
{
    // Text in `game\ce.msg`.
    constexpr int kTextOutOfReach = 326;

    char text[160];
    snprintf(text, sizeof(text), muiText(kTextOutOfReach, "%s is too far away."), critterGetName(critter));
    muiNotify(text, MuiNoticeKind::Warning);
}

void muiDrawItemCell(MuiContext& ui, const MuiRect& rect, Object* item, int quantity, bool pressed)
{
    const MuiTheme& theme = muiTheme();
    muiFillRoundRect(rect, ui.dp(5.0f), pressed ? theme.buttonPressed : theme.button);
    muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);

    int width;
    int height;
    SDL_Texture* texture = muiArtTexture(itemGetInventoryFrmId(item), 0, &width, &height);
    if (texture != nullptr && width > 0 && height > 0) {
        // Integer scale keeps pixels even, smaller art is fitted.
        MuiRect area = rect.inset(ui.dp(4.0f));
        float scale = std::min(area.w / width, area.h / height);
        if (scale >= 1.0f) {
            scale = std::floor(scale);
        }
        float w = width * scale;
        float h = height * scale;
        muiDrawTexture(texture, { area.centerX() - w / 2.0f, area.centerY() - h / 2.0f, w, h });
    }

    if (quantity > 1 || ProtoId(item) == ItemProtoTypeId::Money) {
        std::string label = ProtoId(item) == ItemProtoTypeId::Money ? "$" + std::to_string(quantity) : "x" + std::to_string(quantity);
        float size = ui.dp(11.0f);
        MuiRect labelRect = { rect.x, rect.bottom() - muiLineHeight(size) - ui.dp(2.0f), rect.w - ui.dp(4.0f), muiLineHeight(size) };
        muiDrawTextAligned(muiDecodeUtf8(label.c_str()), labelRect, size, theme.text, MuiAlign::End, MuiAlign::Start);
    }
}

} // namespace fallout
