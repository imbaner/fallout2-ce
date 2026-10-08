#include "touch_hud.h"

#include <string.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "animation.h"
#include "art.h"
#include "combat.h"
#include "critter.h"
#include "dbox.h"
#include "display_monitor.h"
#include "game.h"
#include "game_commands.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "inventory.h"
#include "item.h"
#include "kb.h"
#include "map_defs.h"
#include "map.h"
#include "message.h"
#include "mui.h"
#include "mui_icons.h"
#include "mui_notify.h"
#include "object.h"
#include "settings.h"
#include "mui_screens.h"
#include "party_member.h"
#include "pipboy.h"
#include "proto_types.h"
#include "sfall_ini.h"
#include "sfall_kb_helpers.h"
#include "game_mouse.h"
#include "skill.h"
#include "stat.h"
#include "svga.h"
#include "tile.h"
#include "window_manager.h"

namespace fallout {

namespace {

    // HUD's own actions (`GameCommandType::Hud`; other buttons post the
    // game's commands, the same as the original bar buttons). Taps become
    // commands, so actions run in the game loop, not while drawing.
    constexpr int kEventQuickLoad = 1;
    constexpr int kEventHighlight = 2;
    constexpr int kEventCancelTarget = 3;
    // Attack mode chip `index` - `kEventModeBase + index`.
    constexpr int kEventModeBase = 100;

    // Highlight follows dude's line of sight (the mod updates every 10
    // frames while its key is held).
    constexpr unsigned int kHighlightRefreshMs = 250;
    constexpr size_t kMessagesCapacity = 100;

    // Game modes of screens covering the map, HUD hides under them. "Use
    // item on" is a small panel next to the target in the mobile UI, the
    // map stays visible (like with the skills list).
    constexpr int kScreenGameModes = ~(GameMode::kCombat | GameMode::kPlayerTurn | GameMode::kSpecial | GameMode::kUseOn);

    // Texts in `game\ce.msg` (localized with game data), with English
    // fallbacks for data without them.
    struct HudText {
        int id;
        const char* fallback;
    };

    constexpr HudText kTextQuickLoadConfirm = { 116, "Load current session save?" };

    struct ElementInfo {
        GameCommand command;
        MuiIcon icon;
        // Shown on long press, and next to the icon on wide buttons.
        HudText name;
    };

    constexpr HudText kNoText = { -1, "" };

    // Indexed by `HudElementId`.
    const ElementInfo kElementInfos[static_cast<int>(HudElementId::Count)] = {
        { { GameCommandType::Menu }, MuiIcon::Menu, { 100, "Menu" } }, // Menu
        { { GameCommandType::QuickSave }, MuiIcon::QuickSave, { 101, "Quick save" } }, // QuickSave (game's quick save)
        { { GameCommandType::Hud, kEventQuickLoad }, MuiIcon::QuickLoad, { 102, "Quick load" } }, // QuickLoad
        { { GameCommandType::Inventory }, MuiIcon::Inventory, { 103, "Inventory" } }, // Inventory
        { { GameCommandType::Character }, MuiIcon::Character, { 104, "Character" } }, // Character
        { { GameCommandType::Pipboy }, MuiIcon::Pipboy, { 105, "Pip-Boy" } }, // Pipboy
        { { GameCommandType::Automap }, MuiIcon::Map, { 106, "Map" } }, // Map
        { { GameCommandType::Skilldex }, MuiIcon::Skills, { 107, "Skills" } }, // Skills
        { { GameCommandType::Hud, kEventHighlight }, MuiIcon::Highlight, { 108, "Highlight items" } }, // Highlight
        { { GameCommandType::Sneak }, MuiIcon::Sneak, { 109, "Sneak" } }, // Sneak
        { {}, MuiIcon::Log, { 110, "Messages" } }, // Log (HUD state only)
        { { GameCommandType::EndTurn }, MuiIcon::EndTurn, { 111, "END TURN" } }, // EndTurn
        { { GameCommandType::EndCombat }, MuiIcon::EndCombat, { 112, "END COMBAT" } }, // EndCombat
        { {}, MuiIcon::Count, kNoText }, // Status
        { {}, MuiIcon::Count, kNoText }, // Modes
        { { GameCommandType::SwapHands }, MuiIcon::SwapHands, { 113, "Swap hands" } }, // SwapHands
        { { GameCommandType::UseItem }, MuiIcon::Count, kNoText }, // Weapon (same as the bar item button)
        { {}, MuiIcon::Count, kNoText }, // Indicators
        { {}, MuiIcon::PartyOrders, { 160, "Party orders" } }, // PartyOrders (HUD state only)
    };

    // Orders of the Party Orders mod: the button presses the mod's hotkeys
    // from its ini (DirectInput codes, "35" or "48+29").
    struct PartyOrder {
        const char* iniKey;
        MuiIcon icon;
        HudText name;
    };

    const PartyOrder kPartyOrders[] = {
        { "SETTINGS|HolsterOrderKey", MuiIcon::Holster, { 161, "Holster weapons" } },
        { "SETTINGS|HealingOrderKey", MuiIcon::FilterDrugs, { 162, "Heal yourselves" } },
        { "SETTINGS|LootingOrderKey", MuiIcon::Inventory, { 163, "Loot bodies" } },
        { "SETTINGS|RegroupOrderKey", MuiIcon::Regroup, { 164, "Regroup" } },
        { "SETTINGS|SpreadOrderKey", MuiIcon::Spread, { 165, "Spread out" } },
        { "SETTINGS|PickUpKey", MuiIcon::Use, { 166, "Pick up items" } },
        { "SETTINGS|SwitchKey", MuiIcon::AutoLoot, { 167, "Auto loot by you" } },
        { "BURST_CONTROL|burst_key", MuiIcon::Burst, { 168, "Burst fire" } },
        { "SETTINGS|AmmoTypeOrderKey", MuiIcon::Reload, { 169, "Switch ammo type" } },
    };

    struct AvailableOrder {
        const PartyOrder* order;
        std::vector<int> keys;
    };

    // Names of item actions, the same as the text art of the item button.
    struct ActionName {
        InterfaceFrameId frameId;
        HudText text;
    };

    const ActionName kActionNames[] = {
        { InterfaceFrameId::SingleText, { 124, "SINGLE" } },
        { InterfaceFrameId::BurstText, { 125, "BURST" } },
        { InterfaceFrameId::ThrustText, { 126, "THRUST" } },
        { InterfaceFrameId::SwingText, { 127, "SWING" } },
        { InterfaceFrameId::ThrowText, { 128, "THROW" } },
        { InterfaceFrameId::PunchText, { 129, "PUNCH" } },
        { InterfaceFrameId::KickText, { 130, "KICK" } },
        { InterfaceFrameId::ReloadText, { 131, "RELOAD" } },
        { InterfaceFrameId::UseText, { 132, "USE" } },
        { InterfaceFrameId::UseOnText, { 133, "USE ON" } },
        { InterfaceFrameId::StrongPunch, { 134, "STRONG PUNCH" } },
        { InterfaceFrameId::HammerPunch, { 135, "HAMMER PUNCH" } },
        { InterfaceFrameId::LightningPunch, { 136, "HAYMAKER" } },
        { InterfaceFrameId::ChopPunch, { 137, "JAB" } },
        { InterfaceFrameId::DragonPunch, { 138, "PALM STRIKE" } },
        { InterfaceFrameId::ForcePunch, { 139, "PIERCING STRIKE" } },
        { InterfaceFrameId::StrongKick, { 140, "STRONG KICK" } },
        { InterfaceFrameId::SnapKick, { 141, "SNAP KICK" } },
        { InterfaceFrameId::RoundhouseKick, { 142, "POWER KICK" } },
        { InterfaceFrameId::HipKick, { 143, "HIP KICK" } },
        { InterfaceFrameId::JumpKick, { 144, "HOOK KICK" } },
        { InterfaceFrameId::DeathBlossomKick, { 145, "PIERCING KICK" } },
    };

    // HUD colors on top of the mobile UI theme.
    const MuiColor kPanelColor = muiRgb(0x0B120D, 200);
    const MuiColor kActiveColor = muiRgb(0x1D4A26, 230);
    const MuiColor kBulbGreen = muiRgb(0x3CE03C);
    const MuiColor kBulbYellow = muiRgb(0xF0D030);
    const MuiColor kBulbRed = muiRgb(0xE84030);
    const MuiColor kBulbEmpty = muiRgb(0x2A342C, 220);
    const MuiColor kHpWhite = muiRgb(0xF4F4F0);
    const MuiColor kHpYellow = muiRgb(0xFFE040);
    const MuiColor kHpRed = muiRgb(0xFF4A3A);
    const MuiColor kWarning = muiRgb(0xFFB000);

    enum class ButtonLook {
        Normal,
        Active,
        Disabled,
    };

    struct ModeChip {
        InterfaceItemAction action;
        MuiRect rect;
    };

    class HudScreen : public MuiScreen {
    public:
        HudScreen()
        {
            modal = false;
        }

        void build(MuiContext& ui) override;
        bool isActive() override;
        void back() override;
    };

    HudScreen gHudScreen;

    bool gInitialized = false;
    bool gVisible = false;
    bool gEnabled = true;
    bool gTickerAdded = false;

    bool gCombatButtonsVisible = false;
    bool gCombatButtonsEnabled = false;

    int gActionPoints = 0;
    int gBonusActionPoints = 0;
    int gMaxActionPoints = 10;

    bool gHighlightActive = false;
    unsigned int gHighlightTime = 0;

    // Name of the button held down (long press), drawn over everything.
    std::string gTooltipWidget;
    MuiRect gTooltipAnchor;
    std::u32string gTooltipText;
    bool gTooltipVisible = false;

    std::vector<InterfaceIndicator> gIndicators;

    std::deque<std::string> gMessages;
    bool gLogOpen = false;
    bool gLogScrollToEnd = false;

    std::vector<ModeChip> gModeChips;
    // Selected mode and how many there are when last brought into view.
    int gModeShownKey = -1;

    MessageList gMessageList;
    bool gMessageListLoaded = false;

    // Orders with keys, empty without the mod.
    std::vector<AvailableOrder> gPartyOrders;

    // Element rects of the last frame (output pixels) and output width, for
    // tests.
    std::unordered_map<int, MuiRect> gElementRects;
    float gOutputWidth = 1.0f;

    const char* text(const HudText& text)
    {
        if (gMessageListLoaded && text.id != -1) {
            MessageListItem item;
            item.num = text.id;
            if (messageListGetItem(&gMessageList, &item)) {
                return item.text;
            }
        }

        return text.fallback;
    }

    std::u32string actionName(const InterfaceFrmId& frmId)
    {
        for (const ActionName& name : kActionNames) {
            if (frmId == InterfaceFrmId(name.frameId)) {
                return muiDecodeGameText(text(name.text));
            }
        }
        return std::u32string();
    }

    void sendCommand(const GameCommand& command)
    {
        if (command.type == GameCommandType::None) {
            return;
        }

        _gsound_med_butt_press(-1, 0);
        gameCommandPost(command.type, command.value);
    }

    bool hudVisible()
    {
        return gVisible
            && (GameMode::getCurrentGameMode() & kScreenGameModes) == 0
            && !windowIsModalShown()
            && !muiHidesHud()
            && !mapIsLoading();
    }

    // MARK: Drawing

    void drawPanel(MuiContext& ui, const MuiRect& rect, MuiColor fill, MuiColor border)
    {
        float radius = ui.dp(6.0f);
        muiFillRoundRect(rect, radius, fill);
        muiStrokeRoundRect(rect, radius, ui.dp(1.0f), border);
    }

    // Label in the largest size (up to [maxSize] dp) that fits, two lines at
    // a space if needed.
    void drawLabel(MuiContext& ui, const MuiRect& rect, const std::u32string& label, MuiColor color, float maxSize)
    {
        float available = rect.w - ui.dp(6.0f);
        for (float size = maxSize; size >= 9.0f; size -= 1.0f) {
            float pixels = ui.dp(size);
            if (muiTextWidth(label, pixels) <= available) {
                muiDrawTextAligned(label, rect, pixels, color, MuiAlign::Center, MuiAlign::Center);
                return;
            }
        }

        float pixels = ui.dp(10.0f);
        size_t space = label.find(U' ');
        if (space != std::u32string::npos) {
            float lineHeight = muiLineHeight(pixels);
            MuiRect first = { rect.x, rect.centerY() - lineHeight, rect.w, lineHeight };
            MuiRect second = { rect.x, rect.centerY(), rect.w, lineHeight };
            muiDrawTextAligned(label.substr(0, space), first, pixels, color, MuiAlign::Center, MuiAlign::Center);
            muiDrawTextAligned(label.substr(space + 1), second, pixels, color, MuiAlign::Center, MuiAlign::Center);
        } else {
            muiDrawTextAligned(label, rect, pixels, color, MuiAlign::Center, MuiAlign::Center);
        }
    }

    // Skill the game waits for a target of (chosen in the skills list),
    // SKILL_INVALID otherwise.
    Skill targetingSkill()
    {
        return gameMouseGetModeSkill(gameMouseGetMode());
    }

    ButtonLook buttonLook(HudElementId id)
    {
        switch (id) {
        case HudElementId::Skills:
            return targetingSkill() != SKILL_INVALID ? ButtonLook::Active : ButtonLook::Normal;
        case HudElementId::Highlight:
            return gHighlightActive ? ButtonLook::Active : ButtonLook::Normal;
        case HudElementId::Sneak:
            return gDude != nullptr && dudeHasState(DUDE_STATE_SNEAKING) ? ButtonLook::Active : ButtonLook::Normal;
        case HudElementId::Log:
            return gLogOpen ? ButtonLook::Active : ButtonLook::Normal;
        case HudElementId::EndTurn:
        case HudElementId::EndCombat:
            return gCombatButtonsEnabled ? ButtonLook::Normal : ButtonLook::Disabled;
        default:
            return ButtonLook::Normal;
        }
    }

    std::string elementWidgetId(HudElementId id)
    {
        return "hud." + std::to_string(static_cast<int>(id));
    }

    // Text with a dark shadow, readable over item art.
    void drawShadowedText(MuiContext& ui, const std::u32string& label, const MuiRect& rect, float size, MuiColor color, MuiAlign horizontal, MuiAlign vertical)
    {
        float offset = ui.dp(1.0f);
        MuiRect shadowRect = { rect.x + offset, rect.y + offset, rect.w, rect.h };
        muiDrawTextAligned(label, shadowRect, size, muiRgb(0x000000, static_cast<Uint8>(color.a * 0.8f)), horizontal, vertical);
        muiDrawTextAligned(label, rect, size, color, horizontal, vertical);
    }

    // Keys of an order ("35" or "48+29"), empty when the mod or the key is
    // missing.
    std::vector<int> partyOrderKeys(const char* iniKey)
    {
        std::string triplet = std::string("mods\\party_orders.ini|") + iniKey;
        char value[64];
        std::vector<int> keys;
        if (!sfall_ini_get_string(triplet.c_str(), value, sizeof(value))) {
            return keys;
        }

        const char* cursor = value;
        while (*cursor != '\0') {
            char* end;
            long key = strtol(cursor, &end, 10);
            if (end == cursor) {
                break;
            }
            if (key > 0 && key < 256) {
                keys.push_back(static_cast<int>(key));
            }
            cursor = *end == '+' ? end + 1 : end;
        }
        return keys;
    }

    void loadPartyOrders()
    {
        gPartyOrders.clear();
        for (const PartyOrder& order : kPartyOrders) {
            std::vector<int> keys = partyOrderKeys(order.iniKey);
            if (!keys.empty()) {
                gPartyOrders.push_back({ &order, keys });
            }
        }
    }

    // The Party Orders mod is installed and there is someone to order (a
    // living party member on the map).
    bool partyOrdersAvailable()
    {
        if (gPartyOrders.empty() || gDude == nullptr) {
            return false;
        }

        for (Object* member : get_all_party_members_objects(false)) {
            if (member != gDude) {
                return true;
            }
        }
        return false;
    }

    // List of orders next to the button; choosing one presses its hotkey.
    void openPartyOrders(const MuiRect& button)
    {
        if (muiIsActionListOpen("hud.orders")) {
            muiCloseActionList();
            return;
        }

        std::vector<MuiActionItem> items;
        for (const AvailableOrder& order : gPartyOrders) {
            items.push_back({ order.order->icon, muiDecodeGameText(text(order.order->name)) });
        }

        muiShowActionList(
            "hud.orders", items, button, button,
            [](int index) {
                if (index >= 0 && index < static_cast<int>(gPartyOrders.size())) {
                    _gsound_med_butt_press(-1, 0);
                    const std::vector<int>& keys = gPartyOrders[index].keys;
                    sfall_kb_press_hotkey(keys.data(), static_cast<int>(keys.size()));
                }
            },
            []() { return hudVisible(); });
    }

    // While the game waits for a skill's target: which skill, at the top.
    void buildTargetHint(MuiContext& ui)
    {
        Skill skill = targetingSkill();
        if (skill == SKILL_INVALID) {
            return;
        }

        const MuiTheme& theme = muiTheme();
        std::u32string name = muiDecodeGameText(skillGetName(skill));
        float size = ui.dp(14.0f);
        float iconSize = ui.dp(18.0f);
        float padding = ui.dp(10.0f);
        float width = padding + iconSize + ui.dp(8.0f) + muiTextWidth(name, size) + padding;
        float height = ui.dp(30.0f);
        MuiRect safe = ui.safeRect();
        MuiRect hint = { safe.centerX() - width / 2.0f, safe.y + ui.dp(38.0f), width, height };
        muiFillRoundRect(hint, ui.dp(6.0f), kPanelColor);
        muiStrokeRoundRect(hint, ui.dp(6.0f), ui.dp(1.0f), theme.accent);
        muiDrawIcon(MuiIcon::Target, hint.x + padding + iconSize / 2.0f, hint.centerY(), iconSize, ui.dp(1.6f), theme.accent);
        muiDrawTextAligned(name, { hint.x + padding + iconSize + ui.dp(8.0f), hint.y, width, hint.h }, size, theme.text, MuiAlign::Start, MuiAlign::Center);
    }

    // Long press on a button shows its name instead of acting.
    void updateTooltip(MuiContext& ui, const std::string& widget, const MuiRect& rect, bool pressed, bool longPressed, const HudText& name)
    {
        if (longPressed) {
            gTooltipWidget = widget;
        }

        if (gTooltipWidget != widget) {
            return;
        }

        if (!ui.pointerDown()) {
            gTooltipWidget.clear();
        } else if (pressed) {
            gTooltipAnchor = rect;
            gTooltipText = muiDecodeGameText(text(name));
            gTooltipVisible = true;
        }
    }

    void buildTooltip(MuiContext& ui)
    {
        if (!gTooltipVisible) {
            return;
        }

        const MuiTheme& theme = muiTheme();
        float size = ui.dp(14.0f);
        float padding = ui.dp(10.0f);
        float gap = ui.dp(8.0f);
        MuiRect safe = ui.safeRect();
        float width = std::min(muiTextWidth(gTooltipText, size) + padding * 2.0f, safe.w);
        float height = muiLineHeight(size) + padding;

        // Below buttons in the upper half, above in the lower one.
        float y = gTooltipAnchor.centerY() < safe.centerY()
            ? gTooltipAnchor.bottom() + gap
            : gTooltipAnchor.y - gap - height;
        float x = std::clamp(gTooltipAnchor.centerX() - width / 2.0f, safe.x, safe.right() - width);

        MuiRect bubble = { x, y, width, height };
        muiFillRoundRect(bubble, ui.dp(6.0f), theme.panel);
        muiStrokeRoundRect(bubble, ui.dp(6.0f), ui.dp(1.0f), theme.accent);
        muiDrawTextAligned(gTooltipText, bubble, size, theme.text, MuiAlign::Center, MuiAlign::Center);
    }

    // Icon button; wide buttons (end turn, end combat) also show the name.
    // Buttons on a framed group use the regular button fill.
    void buildButton(MuiContext& ui, HudElementId id, const MuiRect& rect, bool framed)
    {
        const MuiTheme& theme = muiTheme();
        const ElementInfo& info = kElementInfos[static_cast<int>(id)];
        ButtonLook look = buttonLook(id);
        std::string widget = elementWidgetId(id);

        bool pressed;
        bool longPressed;
        bool tapped = ui.touchable(widget, rect, &pressed, &longPressed);
        updateTooltip(ui, widget, rect, pressed, longPressed, info.name);

        std::u32string inventoryCost;
        bool inventoryBlocked = false;
        bool inventoryHasCost = id == HudElementId::Inventory && muiInventoryCost(&inventoryCost, &inventoryBlocked);
        bool unavailable = (id == HudElementId::Pipboy && pipboyUnavailableReason() != PipboyUnavailable::None) || inventoryBlocked;

        MuiColor fill = look == ButtonLook::Active ? kActiveColor : (framed ? theme.button : kPanelColor);
        MuiColor border = look == ButtonLook::Active ? theme.accent : theme.buttonBorder;
        MuiColor color = look == ButtonLook::Disabled ? theme.textDim : theme.accent;
        if (pressed && look != ButtonLook::Disabled) {
            fill = theme.buttonPressed;
            border = theme.accent;
            color = theme.buttonText;
        }

        drawPanel(ui, rect, fill, border);

        float iconSize = std::min(rect.h * 0.5f, ui.dp(22.0f));
        float thickness = ui.dp(1.8f);
        if (rect.w > rect.h * 1.5f) {
            float iconX = rect.x + ui.dp(8.0f) + iconSize / 2.0f;
            muiDrawIcon(info.icon, iconX, rect.centerY(), iconSize * 0.8f, thickness, color);
            MuiRect labelRect = { iconX + iconSize / 2.0f + ui.dp(2.0f), rect.y, rect.right() - iconX - iconSize / 2.0f - ui.dp(4.0f), rect.h };
            drawLabel(ui, labelRect, muiDecodeGameText(text(info.name)), color, 13.0f);
        } else {
            muiDrawIcon(info.icon, rect.centerX(), rect.centerY(), iconSize, thickness, color);
        }

        if (unavailable) {
            muiDrawUnavailableOverlay(ui, rect, 6.0f);
        }

        // In combat: what opening the inventory costs (red: not enough).
        if (inventoryHasCost) {
            muiDrawCostBadge(ui, rect, inventoryCost, inventoryBlocked);
        }

        if (!tapped || !gEnabled || look == ButtonLook::Disabled) {
            return;
        }

        if (id == HudElementId::Log) {
            _gsound_med_butt_press(-1, 0);
            gLogOpen = !gLogOpen;
            gLogScrollToEnd = true;
            return;
        }

        if (id == HudElementId::PartyOrders) {
            _gsound_med_butt_press(-1, 0);
            openPartyOrders(rect);
            return;
        }

        // Skills again while choosing the skill's target: cancel it.
        if (id == HudElementId::Skills && targetingSkill() != SKILL_INVALID) {
            sendCommand({ GameCommandType::Hud, kEventCancelTarget });
            return;
        }

        sendCommand(info.command);
    }

    // MARK: Status (AP and HP)

    // Lightning and AP number, AP bulbs, heart and HP number across the
    // row.
    void buildStatus(MuiContext& ui, const MuiRect& rect)
    {
        if (gDude == nullptr) {
            return;
        }

        const MuiTheme& theme = muiTheme();
        float numberSize = ui.dp(17.0f);
        float iconSize = ui.dp(15.0f);
        float gap = ui.dp(5.0f);
        float bulbGap = ui.dp(2.0f);

        int hp = critterGetHitPoints(gDude);
        int maxHp = critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS);
        MuiColor hpColor = kHpWhite;
        if (hp < static_cast<int>(maxHp * 0.25)) {
            hpColor = kHpRed;
        } else if (hp < static_cast<int>(maxHp * 0.5)) {
            hpColor = kHpYellow;
        }

        // AP are always shown like on the bar: empty out of combat, red on
        // other's turn, number only on dude's turn.
        bool showApNumber = isInCombat() && gActionPoints >= 0;
        int bulbCount = std::clamp(gMaxActionPoints, 1, 16);

        std::u32string hpText = muiDecodeUtf8(std::to_string(std::max(hp, 0)).c_str());
        float apNumberWidth = muiTextWidth(U"10", numberSize);
        float hpNumberWidth = muiTextWidth(U"000", numberSize);

        float x = rect.x + ui.dp(2.0f);
        muiDrawIcon(MuiIcon::ActionPoints, x + iconSize / 2.0f, rect.centerY(), iconSize, ui.dp(1.6f), theme.accent);
        x += iconSize + gap;

        if (showApNumber) {
            std::u32string apText = muiDecodeUtf8(std::to_string(gActionPoints).c_str());
            muiDrawTextAligned(apText, { x, rect.y, apNumberWidth, rect.h }, numberSize, kHpWhite, MuiAlign::End, MuiAlign::Center);
        }
        x += apNumberWidth + gap;

        // HP on the right, bulbs take the rest (up to 9 dp).
        float hpNumberX = rect.right() - ui.dp(2.0f) - hpNumberWidth;
        float heartX = hpNumberX - gap - iconSize;
        muiDrawIcon(MuiIcon::HitPoints, heartX + iconSize / 2.0f, rect.centerY(), iconSize * 0.9f, ui.dp(1.6f), theme.accent);
        muiDrawTextAligned(hpText, { hpNumberX, rect.y, hpNumberWidth, rect.h }, numberSize, hpColor, MuiAlign::End, MuiAlign::Center);

        float available = heartX - gap * 2.0f - x;
        float bulbSize = std::clamp((available + bulbGap) / bulbCount - bulbGap, ui.dp(3.0f), ui.dp(9.0f));
        float bulbY = rect.centerY() - bulbSize / 2.0f;
        for (int index = 0; index < bulbCount; index++) {
            MuiColor color = kBulbEmpty;
            if (gActionPoints == -1) {
                color = kBulbRed;
            } else if (index < gActionPoints) {
                color = kBulbGreen;
            } else if (index < gActionPoints + gBonusActionPoints) {
                color = kBulbYellow;
            }
            muiFillRoundRect({ x, bulbY, bulbSize, bulbSize }, ui.dp(2.0f), color);
            x += bulbSize + bulbGap;
        }
    }

    // MARK: Weapon

    // Item button: item art, action name (top right), AP cost (bottom left),
    // aimed mark (bottom right) like on the bar, ammo bar at the right edge.
    void buildWeapon(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        InterfaceItemButtonInfo info;
        if (!interfaceGetItemButtonInfo(&info)) {
            return;
        }

        bool pressed;
        bool tapped = ui.touchable(elementWidgetId(HudElementId::Weapon), rect, &pressed);
        bool active = pressed && !info.disabled;

        drawPanel(ui, rect, active ? theme.buttonPressed : theme.button, active ? theme.accent : theme.buttonBorder);

        int quantity;
        int capacity;
        bool hasAmmo = !info.disabled && interfaceGetCurrentItemAmmo(&quantity, &capacity);
        float padding = ui.dp(6.0f);
        float barWidth = ui.dp(4.0f);
        MuiRect content = rect.inset(padding);
        if (hasAmmo) {
            content.w -= barWidth + padding;
        }

        Uint8 alpha = info.disabled ? 110 : 255;
        MuiColor color = theme.accent.withAlpha(alpha);

        if (info.itemFrmId.valid()) {
            int width;
            int height;
            SDL_Texture* texture = muiArtTexture(info.itemFrmId, 0, &width, &height);
            if (texture != nullptr) {
                muiDrawTexture(texture, muiFitRect(content, static_cast<float>(width), static_cast<float>(height)), { 255, 255, 255, alpha });
            }
        } else {
            muiDrawIcon(MuiIcon::Fist, content.centerX(), content.centerY(), content.h * 0.6f, ui.dp(2.0f), color);
        }

        float textSize = ui.dp(11.0f);
        if (info.textFrmId.valid()) {
            drawShadowedText(ui, actionName(info.textFrmId), content, textSize, color, MuiAlign::End, MuiAlign::Start);
        }

        if (info.actionPoints >= 0) {
            float iconSize = ui.dp(12.0f);
            float lineHeight = muiLineHeight(ui.dp(13.0f));
            muiDrawIcon(MuiIcon::ActionPoints, content.x + iconSize / 2.0f, content.bottom() - lineHeight / 2.0f, iconSize, ui.dp(1.4f), color);
            MuiRect costRect = { content.x + iconSize + ui.dp(3.0f), content.bottom() - lineHeight, content.w, lineHeight };
            drawShadowedText(ui, muiDecodeUtf8(std::to_string(info.actionPoints).c_str()), costRect, ui.dp(13.0f), kHpWhite.withAlpha(alpha), MuiAlign::Start, MuiAlign::Center);
        }

        if (info.aiming) {
            float aimSize = ui.dp(16.0f);
            muiDrawIcon(MuiIcon::Aim, content.right() - aimSize / 2.0f, content.bottom() - aimSize / 2.0f, aimSize, ui.dp(1.5f), color);
        }

        if (hasAmmo) {
            MuiRect bar = { rect.right() - padding - barWidth, rect.y + padding, barWidth, rect.h - padding * 2.0f };
            muiFillRoundRect(bar, ui.dp(2.0f), kBulbEmpty);
            float ratio = std::clamp(static_cast<float>(quantity) / capacity, 0.0f, 1.0f);
            if (ratio > 0.0f) {
                float filled = bar.h * ratio;
                muiFillRoundRect({ bar.x, bar.bottom() - filled, bar.w, filled }, ui.dp(2.0f), theme.accent);
            }
            muiStrokeRoundRect(bar, ui.dp(2.0f), ui.dp(1.0f), quantity == 0 ? kWarning : theme.buttonBorder);
        }

        if (tapped && gEnabled && !info.disabled) {
            sendCommand(kElementInfos[static_cast<int>(HudElementId::Weapon)].command);
        }
    }

    // Attack modes strip: mode names, aimed modes are a crosshair right after
    // their base mode, reload is an icon. Fixed width, scrolls sideways
    // with a position bar when it overflows.
    void buildModes(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        gModeChips.clear();

        InterfaceItemAction actions[INTERFACE_ITEM_ACTION_COUNT];
        int count = interfaceGetAvailableItemActions(actions, INTERFACE_ITEM_ACTION_COUNT);

        // Only one way to use the item - the weapon button says it all.
        if (count < 2) {
            return;
        }

        InterfaceItemAction current = interfaceGetCurrentItemAction();
        int quantity = 0;
        int capacity = 0;
        bool empty = interfaceGetCurrentItemAmmo(&quantity, &capacity) && quantity == 0;

        struct Chip {
            InterfaceItemAction action;
            MuiIcon icon;
            std::u32string label;
            float chipWidth;
        };

        float textSize = ui.dp(12.0f);
        float padding = ui.dp(10.0f);
        std::vector<Chip> chips;
        int selectedIndex = 0;
        for (int index = 0; index < count; index++) {
            bool aiming;
            InterfaceFrmId frmId = interfaceGetItemActionFrmId(actions[index], &aiming);

            Chip chip = { actions[index], MuiIcon::Count, std::u32string(), rect.h };
            if (aiming) {
                chip.icon = MuiIcon::Aim;
            } else if (actions[index] == INTERFACE_ITEM_ACTION_RELOAD) {
                chip.icon = MuiIcon::Reload;
            } else {
                chip.label = actionName(frmId);
                chip.chipWidth = std::min(muiTextWidth(chip.label, textSize) + padding * 2.0f, rect.w);
            }

            if (actions[index] == current) {
                selectedIndex = index;
            }
            chips.push_back(chip);
        }

        float gap = ui.dp(6.0f);
        float total = 0.0f;
        std::vector<float> starts;
        for (const Chip& chip : chips) {
            total += total > 0.0f ? gap : 0.0f;
            starts.push_back(total);
            total += chip.chipWidth;
        }

        // Chips over the top, the position bar's place under them always
        // (the group keeps its height whatever the weapon).
        float barHeight = ui.dp(3.0f);
        float barGap = ui.dp(3.0f);
        MuiRect strip = { rect.x, rect.y, rect.w, rect.h - barHeight - barGap };
        bool overflow = total > strip.w;

        // A finger moves the modes sideways; a mode chosen anew (the weapon
        // switched, a key) is brought into view once.
        float offset = ui.scrollHorizontal("hud.mode", strip, total);
        int shownKey = static_cast<int>(current) * 64 + count;
        if (shownKey != gModeShownKey) {
            gModeShownKey = shownKey;
            float maxOffset = std::max(total - strip.w, 0.0f);
            float start = starts[selectedIndex];
            float end = start + chips[selectedIndex].chipWidth;
            float wanted = offset;
            if (start < offset) {
                wanted = start;
            } else if (end > offset + strip.w) {
                wanted = end - strip.w;
            } else if (offset == 0.0f && overflow && selectedIndex == count - 1) {
                wanted = maxOffset;
            }
            if (wanted != offset) {
                offset = std::clamp(wanted, 0.0f, maxOffset);
                ui.setScroll("hud.mode", offset);
            }
        }

        // Right aligned when they fit (the group is attached to the right
        // edge).
        float left = overflow ? strip.x - offset : strip.right() - total;

        muiPushClip(strip);
        for (int index = 0; index < count; index++) {
            const Chip& chip = chips[index];
            MuiRect chipRect = { left + starts[index], strip.y, chip.chipWidth, strip.h };
            ModeChip modeChip = { chip.action, { 0, 0, 0, 0 } };
            bool visible = chipRect.right() > strip.x && chipRect.x < strip.right();
            if (visible) {
                // Only its visible part takes touches.
                float touchLeft = std::max(chipRect.x, strip.x);
                float touchRight = std::min(chipRect.right(), strip.right());
                MuiRect touchRect = { touchLeft, chipRect.y, touchRight - touchLeft, chipRect.h };
                modeChip.rect = touchRect;

                bool pressed;
                bool tapped = ui.touchable("hud.mode." + std::to_string(index), touchRect, &pressed);

                bool selected = chip.action == current;
                bool warning = chip.action == INTERFACE_ITEM_ACTION_RELOAD && empty;
                MuiColor fill = pressed ? theme.buttonPressed : (selected ? theme.buttonPrimary : theme.button);
                MuiColor border = selected || pressed ? theme.accent : (warning ? kWarning : theme.buttonBorder);
                MuiColor color = selected && !pressed ? theme.buttonPrimaryText : (warning ? kWarning : theme.accent);
                muiFillRoundRect(chipRect, ui.dp(6.0f), fill);
                muiStrokeRoundRect(chipRect, ui.dp(6.0f), ui.dp(warning ? 2.0f : 1.0f), border);

                if (chip.icon != MuiIcon::Count) {
                    muiDrawIcon(chip.icon, chipRect.centerX(), chipRect.centerY(), strip.h * 0.56f, ui.dp(1.6f), color);
                } else {
                    muiDrawTextAligned(chip.label, chipRect, textSize, color, MuiAlign::Center, MuiAlign::Center);
                }

                if (tapped && gEnabled) {
                    sendCommand({ GameCommandType::Hud, kEventModeBase + index });
                }
            }
            gModeChips.push_back(modeChip);
        }
        muiPopClip();

        // Where the visible part is in the whole list (not touchable).
        if (overflow) {
            MuiRect track = { strip.x, strip.bottom() + barGap, strip.w, barHeight };
            float thumbWidth = std::max(track.w * strip.w / total, ui.dp(16.0f));
            float maxOffset = total - strip.w;
            float thumbX = track.x + (track.w - thumbWidth) * std::clamp(offset / maxOffset, 0.0f, 1.0f);
            muiFillRoundRect(track, barHeight / 2.0f, theme.button);
            muiFillRoundRect({ thumbX, track.y, thumbWidth, track.h }, barHeight / 2.0f, theme.accent);
        }
    }

    // MARK: Indicators

    void buildIndicators(MuiContext& ui, const MuiRect& rect)
    {
        float size = ui.dp(12.0f);
        float padding = ui.dp(8.0f);
        float gap = ui.dp(6.0f);

        std::vector<std::u32string> texts;
        std::vector<float> widths;
        float total = 0.0f;
        for (const InterfaceIndicator& indicator : gIndicators) {
            texts.push_back(muiDecodeGameText(indicator.text));
            widths.push_back(muiTextWidth(texts.back(), size) + padding * 2.0f);
            total += widths.back() + (total > 0.0f ? gap : 0.0f);
        }

        float x = rect.x + std::max((rect.w - total) / 2.0f, 0.0f);
        unsigned char* palette = directDrawGetPalette();
        for (size_t index = 0; index < gIndicators.size(); index++) {
            if (x + widths[index] > rect.right()) {
                break;
            }

            // Indicator color is a palette index (green - good, red - bad,
            // custom for sfall tags).
            Color paletteColor = gIndicators[index].color;
            MuiColor color = {
                static_cast<Uint8>(palette[paletteColor * 3] << 2),
                static_cast<Uint8>(palette[paletteColor * 3 + 1] << 2),
                static_cast<Uint8>(palette[paletteColor * 3 + 2] << 2),
                255,
            };

            MuiRect chip = { x, rect.y, widths[index], rect.h };
            drawPanel(ui, chip, kPanelColor, color);
            muiDrawTextAligned(texts[index], chip, size, color, MuiAlign::Center, MuiAlign::Center);
            x += widths[index] + gap;
        }
    }

    // MARK: Messages

    std::vector<std::u32string> wrapMessages(const std::vector<std::string>& messages, float width, float size, bool bullets)
    {
        std::vector<std::u32string> lines;
        for (const std::string& message : messages) {
            std::u32string text = muiDecodeGameText(message.c_str());
            if (bullets) {
                text = U"• " + text;
            }
            std::vector<std::u32string> wrapped = muiWrapText(text, width, size);
            lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        }
        return lines;
    }

    // New messages pop up next to the log button for a few seconds.
    // Message log panel: scrolls with the finger, newest at the bottom.
    void buildLog(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(14.0f);
        float padding = ui.dp(10.0f);

        drawPanel(ui, rect, muiRgb(0x0B120D, 225), theme.panelBorder);

        std::vector<std::string> messages(gMessages.begin(), gMessages.end());
        MuiRect content = rect.inset(padding);
        std::vector<std::u32string> lines = wrapMessages(messages, content.w, size, true);
        float lineHeight = muiLineHeight(size);
        float height = lines.size() * lineHeight;

        if (gLogScrollToEnd) {
            ui.setScroll("hud.log", std::max(height - content.h, 0.0f));
            gLogScrollToEnd = false;
        }

        float offset = ui.scroll("hud.log", content, height);

        muiPushClip(content);
        float y = content.y - offset + std::max(content.h - height, 0.0f);
        for (const std::u32string& line : lines) {
            if (y + lineHeight >= content.y && y <= content.bottom()) {
                muiDrawText(line, content.x, y, size, theme.text);
            }
            y += lineHeight;
        }
        muiPopClip();
    }

    // MARK: Highlight

    // Highlight button: outlines what can be picked up or searched, by the
    // options of sfall's highlighting mod (`[Highlighting]` of
    // mods\sfall-mods.ini, gl_highlighting.ssl, which works while its key is
    // held) with the same rules and colors, toggled instead of held. Own
    // options in the same section: IncludeNoHighlight=1 - also objects the
    // game keeps out of highlighting (the mod only lets containers through),
    // ExcludePids=a,b,... - protos never outlined.
    struct HighlightConfig {
        bool containers = false;
        bool corpses = false;
        bool critters = false;
        bool checkLineOfSight = false;
        bool includeNoHighlight = false;
        int itemColor = 16;
        int containerColor = 32;
        int containerEmptyColor = 4;
        int corpseColor = 32;
        int corpseEmptyColor = 4;
        int motionScanner = 0;
        std::vector<int> excludedPids;
    };

    constexpr HudText kTextNoMotionSensor = { 170, "You aren't carrying a motion sensor." };
    constexpr HudText kTextMotionSensorEmpty = { 171, "Your motion sensor is out of charge." };

    // Objects outlined by the highlight, with the outline they got. Keys are
    // only compared with live objects, never dereferenced.
    std::unordered_map<Object*, int> gHighlighted;

    int highlightOption(const char* key, int fallback)
    {
        std::string triplet = std::string("mods\\sfall-mods.ini|Highlighting|") + key;
        int value = fallback;
        sfall_ini_get_int(triplet.c_str(), &value);
        return value;
    }

    HighlightConfig readHighlightConfig()
    {
        HighlightConfig config;
        config.containers = highlightOption("Containers", 0) != 0;
        config.corpses = highlightOption("Corpses", 0) != 0;
        config.critters = highlightOption("Critters", 0) != 0;
        config.checkLineOfSight = highlightOption("CheckLOS", 0) != 0;
        config.includeNoHighlight = highlightOption("IncludeNoHighlight", 0) != 0;
        config.motionScanner = highlightOption("MotionScanner", 0);

        // Fallbacks as in the mod.
        config.itemColor = highlightOption("OutlineColor", 16);
        if (config.itemColor < 1) config.itemColor = 64;
        config.containerColor = highlightOption("OutlineColorContainers", 32);
        if (config.containerColor < 1) config.containerColor = 64;
        config.containerEmptyColor = highlightOption("OutlineColorContainersEmpty", 4);
        if (config.containerEmptyColor < 0) config.containerEmptyColor = 64;
        config.corpseColor = highlightOption("OutlineColorCorpses", 32);
        if (config.corpseColor < 1) config.corpseColor = 64;
        config.corpseEmptyColor = highlightOption("OutlineColorCorpsesEmpty", 4);
        if (config.corpseEmptyColor < 0) config.corpseEmptyColor = 64;

        char excluded[512] = "";
        sfall_ini_get_string("mods\\sfall-mods.ini|Highlighting|ExcludePids", excluded, sizeof(excluded));
        for (char* token = strtok(excluded, ", "); token != nullptr; token = strtok(nullptr, ", ")) {
            config.excludedPids.push_back(atoi(token));
        }
        return config;
    }

    // Nothing blocks the sight from dude to the object. The mod checks the
    // line of fire, where people and critter statues standing between hide
    // things at dude's feet; CheckLOS is "only in the player's
    // line-of-sight", so walls and opaque objects block, critters don't.
    bool dudeCanSee(Object* object)
    {
        Object* obstacle = nullptr;
        _make_straight_path_func(gDude, gDude->tile, object->tile, nullptr, &obstacle, 0, _obj_sight_blocking_at);
        return obstacle == nullptr || obstacle == object || obstacle->tile == object->tile;
    }

    bool dudeCanHear(Object* object)
    {
        int distance = critterGetStat(gDude, STAT_PERCEPTION) * 5;
        if ((object->flags & OBJECT_TRANS_GLASS) != 0) {
            distance /= 2;
        }
        return objectGetDistanceBetween(gDude, object) <= distance;
    }

    // Outline the highlight wants for the object, 0 - none.
    int highlightOutline(Object* object, const HighlightConfig& config)
    {
        if ((object->flags & OBJECT_HIDDEN) != 0) {
            return 0;
        }

        int pid = object->pid & 0xFFFFFF;
        if (std::find(config.excludedPids.begin(), config.excludedPids.end(), pid) != config.excludedPids.end()) {
            return 0;
        }

        ObjectType type = FrmId(object).objectType();
        if (type == OBJ_TYPE_ITEM) {
            bool container = itemGetType(object) == ITEM_TYPE_CONTAINER;
            if (container && !config.containers) {
                return 0;
            }
            if ((object->flags & OBJECT_NO_HIGHLIGHT) != 0 && !container && !config.includeNoHighlight) {
                return 0;
            }
            if (config.checkLineOfSight && !dudeCanSee(object)) {
                return 0;
            }
            if (container) {
                return object->data.inventory.length == 0 ? config.containerEmptyColor : config.containerColor;
            }
            return config.itemColor;
        }

        if (type != OBJ_TYPE_CRITTER || object == gDude) {
            return 0;
        }

        if (critterIsDead(object)) {
            if (!config.corpses || critterFlagCheck(object, CRITTER_NO_STEAL)) {
                return 0;
            }
            if (config.checkLineOfSight && !dudeCanSee(object)) {
                return 0;
            }
            return object->data.inventory.length == 0 ? config.corpseEmptyColor : config.corpseColor;
        }

        // Living critters as combat highlights them; in combat the game
        // outlines them itself.
        if (!config.critters || isInCombat()) {
            return 0;
        }
        if (object->data.critter.combat.team == gDude->data.critter.combat.team) {
            return OUTLINE_TYPE_FRIENDLY;
        }
        if (!dudeCanSee(object)) {
            return dudeCanHear(object) ? OUTLINE_TYPE_BLOCKED : 0;
        }
        return OUTLINE_TYPE_HOSTILE;
    }

    void setObjectOutline(Object* object, int outline)
    {
        // The rect of an outlined object covers its outline.
        Rect before;
        objectGetRect(object, &before);
        object->outline = static_cast<OutlineType>(outline);
        Rect after;
        objectGetRect(object, &after);
        rectUnion(&before, &after, &before);
        tileWindowRefreshRect(&before, object->elevation);
    }

    // Outlines what should be outlined on the current elevation now (objects
    // appear, disappear, come into sight) and takes the outline off what
    // shouldn't. Objects outlined by others (combat, the object dude acts on)
    // are left alone.
    void applyHighlight()
    {
        // Entries of other elevations stay, so switching off clears them too.
        HighlightConfig config = readHighlightConfig();
        for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
            auto it = gHighlighted.find(object);
            bool ours = it != gHighlighted.end() && object->outline == it->second;
            if (!ours) {
                if (it != gHighlighted.end()) {
                    gHighlighted.erase(it);
                }
                if (object->outline != OUTLINE_TYPE_NONE) {
                    continue;
                }
            }

            int outline = highlightOutline(object, config);
            if (outline != 0) {
                if (object->outline != outline) {
                    setObjectOutline(object, outline);
                }
                gHighlighted[object] = outline;
            } else if (ours) {
                setObjectOutline(object, OUTLINE_TYPE_NONE);
                gHighlighted.erase(object);
            }
        }
    }

    void clearHighlight()
    {
        for (int elevation = 0; elevation < ELEVATION_COUNT; elevation++) {
            for (Object* object = objectFindFirstAtElevation(elevation); object != nullptr; object = objectFindNextAtElevation()) {
                auto it = gHighlighted.find(object);
                if (it != gHighlighted.end() && object->outline == it->second) {
                    setObjectOutline(object, OUTLINE_TYPE_NONE);
                }
            }
        }
        gHighlighted.clear();
    }

    // The mod's MotionScanner option: 1 - a motion sensor must be carried,
    // 2 - and each switching on takes a charge.
    bool highlightMotionScannerAllows()
    {
        int mode = highlightOption("MotionScanner", 0);
        if (mode == 0) {
            return true;
        }

        Object* scanner = objectGetCarriedObjectByProtoId(gDude, ItemProtoTypeId::MotionSensor);
        if (scanner == nullptr) {
            displayMonitorAddMessage(const_cast<char*>(text(kTextNoMotionSensor)));
            return false;
        }

        if (mode >= 2) {
            if (miscItemGetCharges(scanner) <= 0) {
                displayMonitorAddMessage(const_cast<char*>(text(kTextMotionSensorEmpty)));
                return false;
            }
            miscItemConsumeCharge(scanner);
        }
        return true;
    }

    void setHighlight(bool active)
    {
        if (gHighlightActive == active) {
            return;
        }

        if (active && !highlightMotionScannerAllows()) {
            return;
        }

        gHighlightActive = active;
        gHighlightTime = getTicks();
        if (active) {
            applyHighlight();
        } else {
            clearHighlight();
        }
    }

    void ticker()
    {
        if (!gInitialized) {
            return;
        }

        // Items appear (dropped, map changed), keep them outlined.
        unsigned int now = getTicks();
        if (gHighlightActive && getTicksBetween(now, gHighlightTime) >= kHighlightRefreshMs) {
            gHighlightTime = now;
            if (gGameLoaded) {
                applyHighlight();
            }
        }
    }

    // On the map: closes the log, then opens the game's menu (Esc).
    void HudScreen::back()
    {
        if (gLogOpen) {
            gLogOpen = false;
            return;
        }

        if (gEnabled) {
            gameCommandPost(GameCommandType::Menu);
        }
    }

    bool HudScreen::isActive()
    {
        if (!hudVisible()) {
            gElementRects.clear();
            gModeChips.clear();
            return false;
        }
        return true;
    }

    void HudScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        gElementRects.clear();
        gTooltipVisible = false;
        gOutputWidth = ui.screenRect().w;

        // Layout in output pixels, safe area from the mobile UI.
        MuiRect screen = ui.screenRect();
        MuiRect safe = ui.safeRect();
        HudMetrics metrics;
        metrics.screenWidth = static_cast<int>(screen.w);
        metrics.screenHeight = static_cast<int>(screen.h);
        metrics.pixelsPerDp = ui.pixelsPerDp;
        metrics.insetLeft = static_cast<int>(safe.x);
        metrics.insetTop = static_cast<int>(safe.y);
        metrics.insetRight = static_cast<int>(screen.w - safe.right());
        metrics.insetBottom = static_cast<int>(screen.h - safe.bottom());

        // Party orders only with the mod and someone to order; without the
        // button the column closes up.
        HudLayout layout = hudLayoutLandscape();
        if (!partyOrdersAvailable()) {
            for (HudGroupSpec& group : layout.groups) {
                for (auto& line : group.lines) {
                    line.erase(std::remove_if(line.begin(), line.end(), [](const HudElementSpec& element) {
                        return element.id == HudElementId::PartyOrders;
                    }),
                        line.end());
                }
            }
            // Only the orders list: the same popup serves skills too.
            if (muiIsActionListOpen("hud.orders")) {
                muiCloseActionList();
            }
        }

        std::vector<HudPlacedGroup> groups = hudLayoutPlace(layout, metrics);

        MuiRect logButton;
        MuiRect weaponGroup;
        MuiRect systemGroup;
        for (const HudPlacedGroup& group : groups) {
            MuiRect groupRect = {
                static_cast<float>(group.rect.x),
                static_cast<float>(group.rect.y),
                static_cast<float>(group.rect.width),
                static_cast<float>(group.rect.height),
            };

            if (group.id == HudGroupId::Log) {
                logButton = groupRect;
            } else if (group.id == HudGroupId::Weapon) {
                weaponGroup = groupRect;
            } else if (group.id == HudGroupId::System) {
                systemGroup = groupRect;
            }

            if (group.id == HudGroupId::Combat && !gCombatButtonsVisible) {
                continue;
            }

            // Terminal panel under the group.
            if (group.framed) {
                muiFillRoundRect(groupRect, ui.dp(theme.radius), kPanelColor);
                muiStrokeRoundRect(groupRect, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.panelBorder);
            }

            for (const HudPlacedElement& element : group.elements) {
                MuiRect rect = {
                    groupRect.x + element.rect.x,
                    groupRect.y + element.rect.y,
                    static_cast<float>(element.rect.width),
                    static_cast<float>(element.rect.height),
                };
                gElementRects[static_cast<int>(element.id)] = rect;

                switch (element.id) {
                case HudElementId::Status:
                    buildStatus(ui, rect);
                    break;
                case HudElementId::Modes:
                    buildModes(ui, rect);
                    break;
                case HudElementId::Weapon:
                    buildWeapon(ui, rect);
                    break;
                case HudElementId::Indicators:
                    buildIndicators(ui, rect);
                    break;
                case HudElementId::PartyOrders:
                    buildButton(ui, element.id, rect, group.framed);
                    break;
                default:
                    buildButton(ui, element.id, rect, group.framed);
                    break;
                }
            }
        }

        // Notifications of the log (mui_notify.h) and the log go right of the
        // log button, between left side and weapon groups.
        float gap = ui.dp(6.0f);
        float left = logButton.right() + gap;
        float right = std::min(weaponGroup.x - gap, left + screen.w * 0.55f);
        float bottom = logButton.bottom();

        if (gLogOpen) {
            float top = systemGroup.bottom() + gap;
            buildLog(ui, { left, top, std::max(right - left, ui.dp(120.0f)), std::max(bottom - top, ui.dp(80.0f)) });
            muiNotifyHudArea({}, true);
        } else {
            float toastHeight = ui.dp(4.0f * 26.0f);
            muiNotifyHudArea({ left, bottom - toastHeight, std::min(right - left, screen.w * 0.45f), toastHeight }, false);
        }

        buildTargetHint(ui);
        buildTooltip(ui);
    }

    bool elementCenter(const MuiRect& rect, int* x, int* y)
    {
        if (rect.w <= 0.0f) {
            return false;
        }

        // Output pixels to screen (logical) pixels.
        float scale = screenGetWidth() / gOutputWidth;
        *x = static_cast<int>(rect.centerX() * scale);
        *y = static_cast<int>(rect.centerY() * scale);
        return true;
    }

} // namespace

#if !FALLOUT_TOUCH_ONLY
bool touchHudIsEnabled()
{
    return settings.touch.hud;
}
#endif

bool touchHudHighlightOwns(Object* object)
{
    auto it = gHighlighted.find(object);
    return it != gHighlighted.end() && object->outline == it->second;
}

bool touchHudIsHighlightActive()
{
    return gHighlightActive;
}

void touchHudInit()
{
    if (!touchHudIsEnabled() || gInitialized) {
        return;
    }

    if (messageListInit(&gMessageList)) {
        gMessageListLoaded = messageListLoad(&gMessageList, "game\\ce.msg");
    }

    loadPartyOrders();

    gInitialized = true;

    // Bottom of the mobile UI stack, other screens go above.
    muiPush(&gHudScreen);

    if (!gTickerAdded) {
        tickersAdd(ticker);
        gTickerAdded = true;
    }
}

void touchHudFree()
{
    if (!gInitialized) {
        return;
    }

    if (gTickerAdded) {
        tickersRemove(ticker);
        gTickerAdded = false;
    }

    muiRemove(&gHudScreen);

    if (gMessageListLoaded) {
        messageListFree(&gMessageList);
        gMessageListLoaded = false;
    }

    gMessages.clear();
    gLogOpen = false;
    gVisible = false;
    gInitialized = false;
}

void touchHudShow()
{
    gVisible = true;
}

void touchHudHide()
{
    gVisible = false;
}

void touchHudSetEnabled(bool enabled)
{
    gEnabled = enabled;
}

void touchHudSetActionPoints(int actionPoints, int bonusActionPoints, int maxActionPoints)
{
    gActionPoints = actionPoints;
    gBonusActionPoints = std::max(bonusActionPoints, 0);
    gMaxActionPoints = maxActionPoints;
}

void touchHudSetCombatButtons(bool visible, bool enabled)
{
    gCombatButtonsVisible = visible;
    gCombatButtonsEnabled = visible && enabled;
}

void touchHudAddMessage(const char* message)
{
    if (!touchHudIsEnabled() || message == nullptr) {
        return;
    }

    gMessages.push_back(message);
    while (gMessages.size() > kMessagesCapacity) {
        gMessages.pop_front();
    }

    if (gLogOpen) {
        gLogScrollToEnd = true;
    }
}

void touchHudClearMessages()
{
    gMessages.clear();
}

bool touchHudHandleEvent(int eventCode)
{
    if (!gInitialized) {
        return false;
    }

    switch (eventCode) {
    case kEventQuickLoad:
        // Game's quick load (F7), a tap is easier to hit by mistake than a
        // key, so ask first.
        if (interfaceBarEnabled()) {
            if (showDialogBox(text(kTextQuickLoadConfirm), nullptr, 0, 169, 117, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) != 0) {
                gameCommandPost(GameCommandType::QuickLoad);
            }
        }
        return true;
    case kEventHighlight:
        if (interfaceBarEnabled()) {
            setHighlight(!gHighlightActive);
        }
        return true;
    case kEventCancelTarget:
        gameMouseSetMode(GAME_MOUSE_MODE_MOVE);
        return true;
    }

    if (eventCode >= kEventModeBase && eventCode < kEventModeBase + static_cast<int>(gModeChips.size())) {
        int index = eventCode - kEventModeBase;
        if (interfaceBarEnabled()) {
            interfaceSetCurrentItemAction(gModeChips[index].action);
        }
        return true;
    }

    return false;
}

void touchHudSetIndicators(const InterfaceIndicator* indicators, int count)
{
    gIndicators.assign(indicators, indicators + count);
}

bool touchHudGetElementRect(HudElementId id, MuiRect* rect)
{
    auto it = gElementRects.find(static_cast<int>(id));
    if (!gInitialized || it == gElementRects.end()) {
        return false;
    }

    *rect = it->second;
    return true;
}

bool touchHudGetElementScreenCenter(HudElementId id, int* x, int* y)
{
    auto it = gElementRects.find(static_cast<int>(id));
    if (!gInitialized || it == gElementRects.end()) {
        return false;
    }

    return elementCenter(it->second, x, y);
}

bool touchHudGetModeScreenCenter(int index, int* x, int* y)
{
    if (!gInitialized || index < 0 || index >= static_cast<int>(gModeChips.size())) {
        return false;
    }

    return elementCenter(gModeChips[index].rect, x, y);
}

} // namespace fallout
