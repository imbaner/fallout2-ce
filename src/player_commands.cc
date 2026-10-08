#include "player_commands.h"

#include <algorithm>

#include "actions.h"
#include "animation.h"
#include "combat.h"
#include "critter.h"
#include "interface.h"
#include "item.h"
#include "object.h"
#include "proto_instance.h"
#include "settings.h"
#include "tile.h"

namespace fallout {

Object* playerObjectAt(int worldX, int worldY, ObjectType objectType, bool includeDude, int elevation)
{
    bool intersectsRoof = false;
    if (objectType == -1) {
        if (_square_roof_intersect(worldX, worldY, elevation)) {
            if (_obj_intersects_with(gEgg, worldX, worldY) == 0) {
                intersectsRoof = true;
            }
        }
    }

    Object* found = nullptr;
    if (!intersectsRoof) {
        ObjectWithFlags* entries;
        int count = _obj_create_intersect_list(worldX, worldY, elevation, objectType, &entries);
        for (int index = count - 1; index >= 0; index--) {
            ObjectWithFlags* ptr = &(entries[index]);
            if (includeDude || gDude != ptr->object) {
                found = ptr->object;
                if ((ptr->flags & OBJECT_HIDDEN) != OBJECT_NONE) {
                    if ((ptr->flags & OBJECT_NO_SAVE) == OBJECT_NONE) {
                        if (FrmId(ptr->object).objectType() != OBJ_TYPE_CRITTER || (ptr->object->data.critter.combat.results & (DAM_KNOCKED_OUT | DAM_DEAD)) == 0) {
                            break;
                        }
                    }
                }
            }
        }

        if (count != 0) {
            _obj_delete_intersect_list(&entries);
        }
    }
    return found;
}

Object* playerPrimaryTargetAt(int worldX, int worldY, int elevation)
{
    Object* target = playerObjectAt(worldX, worldY, OBJ_TYPE_INVALID, true, elevation);
    if (target == nullptr) {
        return nullptr;
    }

    switch (FrmId(target).objectType()) {
    case OBJ_TYPE_WALL:
    case OBJ_TYPE_SCENERY:
    case OBJ_TYPE_MISC: {
        // Outlined item in front of or behind the object (must be in sync
        // with `gameMouseRefresh`).
        Object* item = playerObjectAt(worldX, worldY, OBJ_TYPE_ITEM, true, elevation);
        if (objectHasVisibleOutline(item)) {
            return item;
        }
        break;
    }
    default:
        break;
    }

    return target;
}

void playerPrimaryAction(Object* target)
{
    if (target == nullptr) {
        return;
    }

    ObjectType objectType = FrmId(target).objectType();
    switch (objectType) {
    case OBJ_TYPE_WALL:
    case OBJ_TYPE_SCENERY:
    case OBJ_TYPE_MISC:
        if (objectType == OBJ_TYPE_SCENERY && _obj_action_can_use(target)) {
            _action_use_an_object(gDude, target);
        } else if (objectType != OBJ_TYPE_MISC) {
            playerLook(target);
        }
        break;
    case OBJ_TYPE_ITEM:
        actionPickUp(gDude, target);
        break;
    case OBJ_TYPE_CRITTER:
        if (target == gDude) {
            if (FrmId(gDude).animationType() == ANIM_STAND) {
                Rect dudeRect;
                if (objectRotateClockwise(target, &dudeRect) == 0) {
                    tileWindowRefreshRect(&dudeRect, target->elevation);
                }
            }
        } else if (_obj_action_can_talk_to(target)) {
            if (isInCombat()) {
                playerLook(target);
            } else {
                actionTalk(gDude, target);
            }
        } else {
            actionLootCritter(gDude, target);
        }
        break;
    default:
        break;
    }
}

int playerMoveTo(int tile, bool alternate)
{
    int actionPoints = isInCombat()
        ? _combat_free_move + gDude->data.critter.combat.ap
        : -1;

    bool run = settings.preferences.running != alternate;
    return run
        ? dudeRunToTile(tile, actionPoints)
        : dudeMoveToTile(tile, actionPoints);
}

void playerAttack(Object* target)
{
    if (target != nullptr) {
        _combat_attack_this(target);
    }
}

void playerUseActiveItemOn(Object* target)
{
    Object* item;
    if (target == nullptr || interfaceGetActiveItem(&item) == -1) {
        return;
    }

    if (!isInCombat()) {
        _action_use_an_item_on_object(gDude, target, item);
        return;
    }

    HitMode hitMode = interfaceGetCurrentHand()
        ? HIT_MODE_RIGHT_WEAPON_PRIMARY
        : HIT_MODE_LEFT_WEAPON_PRIMARY;

    int actionPointsRequired = itemGetActionPointCost(gDude, hitMode, false);
    if (actionPointsRequired > gDude->data.critter.combat.ap) {
        return;
    }

    if (_action_use_an_item_on_object(gDude, target, item) != -1) {
        gDude->data.critter.combat.ap = std::max(gDude->data.critter.combat.ap - actionPointsRequired, 0);
        interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
    }
}

int playerUseSkillOn(Skill skill, Object* target)
{
    return actionUseSkill(gDude, target, skill);
}

void playerLook(Object* target)
{
    if (objectExamine(gDude, target) == -1) {
        objectLookAt(gDude, target);
    }
}

bool playerGetHitChance(Object* target, int* chance)
{
    return _combat_to_hit(target, chance);
}

int playerGetMoveCost(int tile)
{
    int distance = _make_path(gDude, gDude->tile, tile, nullptr, 1);
    if (distance == 0) {
        return -1;
    }

    return std::max(0, critterGetMovementPointCostAdjustedForCrippledLegs(gDude, distance) - _combat_free_move);
}

} // namespace fallout
