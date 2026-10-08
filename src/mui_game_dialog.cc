#include "mui.h"
#include "mui_icons.h"
#include "mui_screens.h"

#include <algorithm>
#include <string>
#include <vector>

#include "combat.h"
#include "game.h"
#include "game_commands.h"
#include "game_dialog.h"
#include "game_sound.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "kb.h"
#include "message.h"
#include "mui_notify.h"
#include "object.h"
#include "pipboy.h"
#include "skill.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x060A07);
    const MuiColor kReactionGood = muiRgb(0x78B4FF);
    const MuiColor kReactionBad = muiRgb(0xFF6E5E);

    // Changed, not applied yet (sections, settings).
    const MuiColor kSectionMarkColor = muiRgb(0xFFB000);

    // Texts in `game\ce.msg`.
    constexpr int kTextPartyStats = 172;
    constexpr int kTextPartyWeapon = 180;
    constexpr int kTextPartyArmor = 181;
    constexpr int kTextPartyBestWeapon = 182;
    constexpr int kTextPartyBestArmor = 183;
    constexpr int kTextPartyDisposition = 184;
    constexpr int kTextPartyDispositions = 185;

    const char* const kPartyStatFallbacks[] = { "Hit points", "Skill", "Carrying", "Melee damage", "APs", "Level", "AC", "Addict" };
    const char* const kPartyDispositionFallbacks[kPartyControlDispositionCount] = { "Custom", "Coward", "Defensive", "Aggressive", "Berserk" };

    // Icons of the party member settings (burst, run away, weapon, distance,
    // attack who, chems).
    const MuiIcon kPartySettingIcons[kPartyControlSettingCount] = {
        MuiIcon::Burst,
        MuiIcon::Back,
        MuiIcon::FilterWeapons,
        MuiIcon::Spread,
        MuiIcon::Target,
        MuiIcon::FilterDrugs,
    };

    std::u32string partyText(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    // Talk screen: speaker (talking head or map around the speaker, drawn by
    // the game) on the left with the reply below it, options on the right,
    // tabs at the right edge. Reply, options and review scroll in their own
    // areas. Party members also get the party member control panel (the
    // game's combat control and customization panels in one): their stats
    // under the head instead of the reply.
    class TalkScreen : public MuiScreen {
    public:
        bool reviewOpen = false;
        bool reviewScrollToEnd = false;
        std::string lastReply;
        int lastOptionCount = -1;
        unsigned int headVersion = 0;
        std::vector<unsigned char> head;

        // Review requested by the barter screen's review tab.
        bool reviewPending = false;

        bool partyOpen = false;
        bool partyPending = false;

        bool coversScreen() override { return true; }

        // Closes the review or party panel (the game's talk has no back:
        // answers end it).
        void back() override
        {
            reviewOpen = false;
            partyOpen = false;
        }

        bool isActive() override
        {
            bool talking = gameDialogIsTalking();
            modal = talking;
            if (!talking) {
                reviewOpen = false;
                partyOpen = false;
            } else if (reviewPending) {
                reviewPending = false;
                reviewOpen = true;
                partyOpen = false;
                reviewScrollToEnd = true;
            } else if (partyPending) {
                partyPending = false;
                partyOpen = true;
                reviewOpen = false;
            }

            if (partyOpen && !gameDialogSpeakerIsPartyMember()) {
                partyOpen = false;
            }
            return talking;
        }

        // Talk and barter screens switch without the map showing between
        // them (while the game switches neither is up).
        bool holdsFrame() override
        {
            BarterView view;
            return !gameDialogIsTalking() && gameDialogIsInBarter() && !barterGetView(&view);
        }

        void build(MuiContext& ui) override;

    private:
        void buildHead(MuiContext& ui, const MuiRect& rect);
        void buildIconButtons(MuiContext& ui, const MuiRect& column);
        void buildReply(MuiContext& ui, const MuiRect& rect);
        void buildOptions(MuiContext& ui, const MuiRect& rect);
        void buildReview(MuiContext& ui, const MuiRect& rect);
        void buildPartyStats(MuiContext& ui, const MuiRect& rect, const PartyControlView& view);
        void buildParty(MuiContext& ui, const MuiRect& rect, const PartyControlView& view);
        bool buildPartyEquipment(MuiContext& ui, const MuiRect& rect, const std::string& id, int label, Object* item, int button, bool enabled);
    };

    TalkScreen* gTalkScreen = nullptr;

    void TalkScreen::build(MuiContext& ui)
    {
        // Opaque over the map.
        muiFillRect(ui.screenRect(), kBackground);

        MuiRect content;
        MuiRect iconColumn;
        muiDialogLayout(ui, &content, &iconColumn);
        float gap = ui.dp(10.0f);

        MuiRect left = { content.x, content.y, (iconColumn.right() - content.x) * 0.46f, content.h };
        MuiRect right = { left.right() + gap, content.y, content.right() - left.right() - gap, content.h };

        // New reply: show it from the top.
        const char* reply = gameDialogGetReplyText();
        int optionCount = gameDialogGetOptionCount();
        if (lastReply != reply || lastOptionCount != optionCount) {
            lastReply = reply;
            lastOptionCount = optionCount;
            ui.setScroll("talk.reply", 0.0f);
            ui.setScroll("talk.options", 0.0f);
        }

        // Speaker picture keeps the game's aspect (388 x 200) across the
        // left column, the reply takes the rest below it.
        float headHeight = std::min(left.w * 200.0f / 388.0f, left.h * 0.6f);
        buildHead(ui, { left.x, left.y, left.w, headHeight });
        MuiRect below = { left.x, left.y + headHeight + gap, left.w, left.h - headHeight - gap };

        PartyControlView partyView;
        if (partyOpen && gameDialogGetPartyControlView(&partyView)) {
            buildPartyStats(ui, below, partyView);
            buildParty(ui, right, partyView);
        } else {
            buildReply(ui, below);
            if (reviewOpen) {
                buildReview(ui, right);
            } else {
                buildOptions(ui, right);
            }
        }

        buildIconButtons(ui, iconColumn);
    }

    void TalkScreen::buildHead(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        // Talking head as the game draws it (animated, lips), or the map
        // around the speaker.
        const unsigned char* data;
        int width;
        int height;
        int pitch;
        if (!gameDialogGetHeadImage(&data, &width, &height, &pitch)) {
            return;
        }

        head.resize(static_cast<size_t>(width) * height);
        for (int row = 0; row < height; row++) {
            std::copy(data + row * pitch, data + row * pitch + width, head.begin() + row * width);
        }

        SDL_Texture* texture = muiIndexedTexture("talk.head", head.data(), width, height, ++headVersion);
        MuiRect headRect = muiFitRect(rect, static_cast<float>(width), static_cast<float>(height));
        muiFillRoundRect(headRect, ui.dp(6.0f), kBackground);
        muiDrawTexture(texture, headRect);
        muiStrokeRoundRect(headRect.inset(-ui.dp(1.0f)), ui.dp(6.0f), ui.dp(theme.borderWidth), theme.panelBorder);
    }

    // Tabs at the right edge: talk, barter (traders only), review, party
    // member control (party members only).
    void TalkScreen::buildIconButtons(MuiContext& ui, const MuiRect& column)
    {
        MuiDialogTab active = partyOpen ? MuiDialogTab::Party : (reviewOpen ? MuiDialogTab::Review : MuiDialogTab::Talk);
        int tab = muiDialogTabs(ui, column, active, gameDialogSpeakerCanBarter(), gameDialogSpeakerIsPartyMember());
        switch (tab) {
        case static_cast<int>(MuiDialogTab::Talk):
            reviewOpen = false;
            partyOpen = false;
            break;
        case static_cast<int>(MuiDialogTab::Barter):
            reviewOpen = false;
            partyOpen = false;
            gameDialogRequestBarter();
            break;
        case static_cast<int>(MuiDialogTab::Review):
            reviewOpen = true;
            partyOpen = false;
            reviewScrollToEnd = true;
            break;
        case static_cast<int>(MuiDialogTab::Party):
            partyOpen = true;
            reviewOpen = false;
            break;
        }
    }

    void TalkScreen::buildReply(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        ui.panel(rect);

        float size = ui.dp(17.0f);
        MuiRect content = rect.inset(ui.dp(12.0f), ui.dp(10.0f));

        // Speaker name as the header.
        if (gGameDialogSpeaker != nullptr) {
            float nameSize = ui.dp(15.0f);
            float nameHeight = muiLineHeight(nameSize);
            muiDrawTextAligned(muiDecodeGameText(objectGetName(gGameDialogSpeaker)), { content.x, content.y, content.w, nameHeight }, nameSize, theme.accent, MuiAlign::Start, MuiAlign::Start);
            content.y += nameHeight + ui.dp(4.0f);
            content.h -= nameHeight + ui.dp(4.0f);
        }

        std::vector<std::u32string> lines = muiWrapText(muiDecodeGameText(gameDialogGetReplyText()), content.w, size);
        float lineHeight = muiLineHeight(size);
        float height = lines.size() * lineHeight;

        float offset = ui.scroll("talk.reply", content, height);

        muiPushClip(content);
        float y = content.y - offset;
        for (const std::u32string& line : lines) {
            if (y + lineHeight >= content.y && y <= content.bottom()) {
                muiDrawText(line, content.x, y, size, theme.text);
            }
            y += lineHeight;
        }
        muiPopClip();
    }

    void TalkScreen::buildOptions(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        float size = ui.dp(16.0f);
        float padding = ui.dp(10.0f);
        float gap = ui.dp(6.0f);
        float minHeight = ui.dp(46.0f);
        float lineHeight = muiLineHeight(size);

        int count = gameDialogGetOptionCount();
        std::vector<std::vector<std::u32string>> texts;
        std::vector<float> heights;
        float total = 0.0f;
        for (int index = 0; index < count; index++) {
            texts.push_back(muiWrapText(muiDecodeGameText(gameDialogGetOptionText(index)), rect.w - padding * 2.0f, size));
            heights.push_back(std::max(texts.back().size() * lineHeight + padding * 1.4f, minHeight));
            total += heights.back() + (index > 0 ? gap : 0.0f);
        }

        float offset = ui.scroll("talk.options", rect, total);
        bool scrolling = ui.isScrolling("talk.options");

        muiPushClip(rect);
        float y = rect.y - offset;
        for (int index = 0; index < count; index++) {
            MuiRect optionRect = { rect.x, y, rect.w, heights[index] };
            y += heights[index] + gap;

            if (optionRect.bottom() < rect.y || optionRect.y > rect.bottom()) {
                continue;
            }

            // Visible part only, so a half hidden option can't be tapped
            // outside the area.
            MuiRect touchRect = optionRect;
            touchRect.y = std::max(optionRect.y, rect.y);
            touchRect.h = std::min(optionRect.bottom(), rect.bottom()) - touchRect.y;

            bool pressed;
            bool tapped = ui.touchable("talk.options." + std::to_string(index), touchRect, &pressed);

            muiFillRoundRect(optionRect, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.button);
            muiStrokeRoundRect(optionRect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);

            MuiColor color = theme.text;
            switch (gameDialogGetOptionReaction(index)) {
            case 0:
                color = kReactionGood;
                break;
            case 2:
                color = kReactionBad;
                break;
            default:
                break;
            }

            float textY = optionRect.y + (optionRect.h - texts[index].size() * lineHeight) / 2.0f;
            for (const std::u32string& line : texts[index]) {
                muiDrawText(line, optionRect.x + padding, textY, size, color);
                textY += lineHeight;
            }

            if (tapped && !scrolling) {
                _gsound_red_butt_press(-1, 0);
                gameDialogChooseOption(index);
            }
        }
        muiPopClip();
    }

    // Everything said in this conversation, like the game's review window.
    void TalkScreen::buildReview(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        ui.panel(rect);

        float size = ui.dp(16.0f);
        float lineHeight = muiLineHeight(size);
        float paragraphGap = ui.dp(8.0f);
        MuiRect content = rect.inset(ui.dp(12.0f), ui.dp(10.0f));

        struct Line {
            std::u32string text;
            MuiColor color;
            float gapBefore;
        };

        std::u32string speaker = gGameDialogSpeaker != nullptr ? muiDecodeGameText(objectGetName(gGameDialogSpeaker)) : U"";
        std::u32string dude = muiDecodeGameText(objectGetName(gDude));

        std::vector<Line> lines;
        int count = gameDialogGetReviewCount();
        for (int index = 0; index < count; index++) {
            const char* reply;
            const char* option;
            gameDialogGetReviewEntry(index, &reply, &option);

            bool first = true;
            for (const std::u32string& text : muiWrapText(speaker + U": " + muiDecodeGameText(reply), content.w, size)) {
                lines.push_back({ text, theme.text, first && !lines.empty() ? paragraphGap : 0.0f });
                first = false;
            }

            if (option != nullptr) {
                first = true;
                for (const std::u32string& text : muiWrapText(dude + U": " + muiDecodeGameText(option), content.w, size)) {
                    lines.push_back({ text, theme.accent, first ? paragraphGap / 2.0f : 0.0f });
                    first = false;
                }
            }
        }

        float height = 0.0f;
        for (const Line& line : lines) {
            height += line.gapBefore + lineHeight;
        }

        if (reviewScrollToEnd) {
            ui.setScroll("talk.review", std::max(height - content.h, 0.0f));
            reviewScrollToEnd = false;
        }

        float offset = ui.scroll("talk.review", content, height);

        muiPushClip(content);
        float y = content.y - offset;
        for (const Line& line : lines) {
            y += line.gapBefore;
            if (y + lineHeight >= content.y && y <= content.bottom()) {
                muiDrawText(line.text, content.x, y, size, line.color);
            }
            y += lineHeight;
        }
        muiPopClip();
    }

    // Stats of the combat control panel under the head.
    void TalkScreen::buildPartyStats(MuiContext& ui, const MuiRect& rect, const PartyControlView& view)
    {
        const MuiTheme& theme = muiTheme();
        ui.panel(rect);

        float size = ui.dp(15.0f);
        float lineHeight = muiLineHeight(size) + ui.dp(4.0f);
        MuiRect content = rect.inset(ui.dp(12.0f), ui.dp(10.0f));

        float nameSize = ui.dp(15.0f);
        float nameHeight = muiLineHeight(nameSize);
        muiDrawTextAligned(muiDecodeGameText(objectGetName(gGameDialogSpeaker)), { content.x, content.y, content.w, nameHeight }, nameSize, theme.accent, MuiAlign::Start, MuiAlign::Start);
        content.y += nameHeight + ui.dp(6.0f);
        content.h -= nameHeight + ui.dp(6.0f);

        struct Stat {
            int label;
            std::string value;
            bool warning;
        };

        std::vector<Stat> stats = {
            { 0, std::to_string(view.hitPoints) + "/" + std::to_string(view.maximumHitPoints), false },
            { 1, skillGetName(view.bestSkill), false },
            { 2, std::to_string(view.weight) + "/" + std::to_string(view.carryWeight), view.encumbered },
            { 3, std::to_string(view.meleeDamage), false },
            { 4, std::to_string(view.actionPoints) + "/" + std::to_string(view.maximumActionPoints), false },
        };
        if (view.extraInfo) {
            stats.push_back({ 5, std::to_string(view.level), false });
            stats.push_back({ 6, std::to_string(view.armorClass), false });
            if (view.addicted) {
                stats.push_back({ 7, std::string(), true });
            }
        }

        float contentHeight = stats.size() * lineHeight;
        float offset = ui.scroll("talk.party.stats", content, contentHeight);

        muiPushClip(content);
        float y = content.y - offset;
        for (const Stat& stat : stats) {
            MuiRect row = { content.x, y, content.w, lineHeight };
            MuiColor valueColor = stat.warning ? muiRgb(0xFF6E5E) : theme.accent;
            if (stat.value.empty()) {
                muiDrawTextAligned(partyText(kTextPartyStats + stat.label, kPartyStatFallbacks[stat.label]), row, size, valueColor, MuiAlign::Start, MuiAlign::Center);
            } else {
                muiDrawTextAligned(partyText(kTextPartyStats + stat.label, kPartyStatFallbacks[stat.label]), row, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
                muiDrawTextAligned(muiDecodeGameText(stat.value.c_str()), row, size, valueColor, MuiAlign::End, MuiAlign::Center);
            }
            y += lineHeight;
        }
        muiPopClip();
    }

    // Weapon or armor with its "use best" button; returns true if the button
    // was tapped.
    bool TalkScreen::buildPartyEquipment(MuiContext& ui, const MuiRect& rect, const std::string& id, int label, Object* item, int button, bool enabled)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(6.0f);

        std::u32string buttonText = partyText(button, button == kTextPartyBestWeapon ? "Use best weapon" : "Use best armor");
        float buttonTextSize = ui.dp(13.0f);
        // Both buttons as wide as the longer text, so the slots line up.
        float textWidth = std::max(muiTextWidth(partyText(kTextPartyBestWeapon, "Use best weapon"), buttonTextSize),
            muiTextWidth(partyText(kTextPartyBestArmor, "Use best armor"), buttonTextSize));
        float buttonWidth = std::min(textWidth + ui.dp(20.0f), rect.w * 0.45f);
        MuiRect slot = { rect.x, rect.y, rect.w - buttonWidth - gap, rect.h };
        MuiRect buttonRect = { slot.right() + gap, rect.y, buttonWidth, rect.h };

        muiFillRoundRect(slot, ui.dp(6.0f), theme.button);
        muiStrokeRoundRect(slot, ui.dp(6.0f), ui.dp(1.0f), theme.buttonBorder);

        // Item picture.
        MuiRect art = { slot.x + ui.dp(4.0f), slot.y + ui.dp(4.0f), slot.h - ui.dp(8.0f), slot.h - ui.dp(8.0f) };
        if (item != nullptr) {
            int width;
            int height;
            SDL_Texture* texture = muiArtTexture(itemGetInventoryFrmId(item), 0, &width, &height);
            if (texture != nullptr) {
                muiDrawTexture(texture, muiFitRect(art, static_cast<float>(width), static_cast<float>(height)));
            }
        }

        MuiRect text = { art.right() + ui.dp(6.0f), slot.y + ui.dp(4.0f), slot.right() - art.right() - ui.dp(10.0f), slot.h - ui.dp(8.0f) };
        float labelSize = ui.dp(11.0f);
        muiDrawTextAligned(partyText(label, label == kTextPartyWeapon ? "Weapon" : "Armor"), { text.x, text.y, text.w, text.h / 2.0f }, labelSize, theme.textDim, MuiAlign::Start, MuiAlign::Center);
        std::u32string name = item != nullptr ? muiDecodeGameText(itemGetName(item)) : std::u32string(U"—");
        float nameSize = muiFitTextSize(name, text.w, ui.dp(14.0f), ui.dp(10.0f));
        muiPushClip(text);
        muiDrawTextAligned(name, { text.x, text.y + text.h / 2.0f, text.w, text.h / 2.0f }, nameSize, theme.text, MuiAlign::Start, MuiAlign::Center);
        muiPopClip();

        bool pressed;
        bool tapped = ui.touchable(id, buttonRect, &pressed) && enabled;
        pressed = pressed && enabled;
        muiFillRoundRect(buttonRect, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(buttonRect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);
        float size = muiFitTextSize(buttonText, buttonRect.w - ui.dp(12.0f), buttonTextSize, ui.dp(9.0f));
        muiDrawTextAligned(buttonText, buttonRect, size, enabled ? (pressed ? theme.buttonText : theme.accent) : theme.textDim, MuiAlign::Center, MuiAlign::Center);
        return tapped;
    }

    // Equipment, dispositions and the six settings (their values are the
    // chosen disposition's; changing one switches to the custom one, as in
    // the game).
    void TalkScreen::buildParty(MuiContext& ui, const MuiRect& rect, const PartyControlView& view)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(6.0f);
        float equipmentHeight = ui.dp(52.0f);
        float titleHeight = ui.dp(24.0f);
        float dispositionHeight = ui.dp(40.0f);
        float settingHeight = ui.dp(42.0f);

        float total = equipmentHeight * 2.0f + gap * 2.0f + titleHeight + dispositionHeight + gap + kPartyControlSettingCount * settingHeight;
        float offset = ui.scroll("talk.party", rect, total);
        bool scrolling = ui.isScrolling("talk.party");

        muiPushClip(rect);
        float y = rect.y - offset;

        if (buildPartyEquipment(ui, { rect.x, y, rect.w, equipmentHeight }, "talk.party.weapon", kTextPartyWeapon, view.weapon, kTextPartyBestWeapon, true) && !scrolling) {
            _gsound_red_butt_press(-1, 0);
            gameDialogQueuePartyControlAction(PartyControlAction::UseBestWeapon);
        }
        y += equipmentHeight + gap;

        if (buildPartyEquipment(ui, { rect.x, y, rect.w, equipmentHeight }, "talk.party.armor", kTextPartyArmor, view.armor, kTextPartyBestArmor, view.canEquipArmor) && !scrolling) {
            _gsound_red_butt_press(-1, 0);
            gameDialogQueuePartyControlAction(PartyControlAction::UseBestArmor);
        }
        y += equipmentHeight + gap;

        muiDrawTextAligned(partyText(kTextPartyDisposition, "Disposition"), { rect.x, y, rect.w, titleHeight }, ui.dp(13.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        y += titleHeight;

        // Dispositions in one row.
        float cellGap = ui.dp(4.0f);
        float cellWidth = (rect.w - cellGap * (kPartyControlDispositionCount - 1)) / kPartyControlDispositionCount;
        for (int disposition = 0; disposition < kPartyControlDispositionCount; disposition++) {
            MuiRect cell = { rect.x + disposition * (cellWidth + cellGap), y, cellWidth, dispositionHeight };
            bool supported = view.dispositionSupported[disposition];
            bool active = view.disposition == disposition;

            bool pressed;
            bool tapped = ui.touchable("talk.party.disposition." + std::to_string(disposition), cell, &pressed) && supported;
            pressed = pressed && supported;

            MuiColor fill = pressed ? theme.buttonPressed : (active ? theme.buttonPrimary : theme.button);
            muiFillRoundRect(cell, ui.dp(6.0f), fill);
            muiStrokeRoundRect(cell, ui.dp(6.0f), ui.dp(1.0f), active || pressed ? theme.accent : theme.buttonBorder);

            std::u32string label = partyText(kTextPartyDispositions + disposition, kPartyDispositionFallbacks[disposition]);
            float size = muiFitTextSize(label, cell.w - ui.dp(6.0f), ui.dp(13.0f), ui.dp(8.0f));
            MuiColor color = !supported ? theme.textDim : (active && !pressed ? theme.buttonPrimaryText : theme.accent);
            muiDrawTextAligned(label, cell, size, color, MuiAlign::Center, MuiAlign::Center);

            if (tapped && !active && !scrolling) {
                _gsound_red_butt_press(-1, 0);
                gameDialogQueuePartyControlAction(PartyControlAction::SetDisposition, disposition);
            }
        }
        y += dispositionHeight + gap;

        // Settings: tap opens the values (ones the party member can't use are
        // dimmed).
        float size = ui.dp(14.0f);
        for (int setting = 0; setting < kPartyControlSettingCount; setting++) {
            MuiRect row = { rect.x, y, rect.w, settingHeight };
            y += settingHeight;

            bool pressed;
            bool tapped = ui.touchable("talk.party.setting." + std::to_string(setting), row, &pressed);
            if (pressed) {
                muiFillRoundRect(row, ui.dp(6.0f), theme.buttonPressed);
            }
            if (setting > 0) {
                muiFillRect({ row.x + ui.dp(8.0f), row.y, row.w - ui.dp(16.0f), ui.dp(1.0f) }, theme.panelBorder);
            }

            float iconSize = ui.dp(18.0f);
            muiDrawIcon(kPartySettingIcons[setting], row.x + ui.dp(8.0f) + iconSize / 2.0f, row.centerY(), iconSize, ui.dp(1.6f), theme.accent);

            MuiRect text = { row.x + ui.dp(16.0f) + iconSize, row.y, row.w - ui.dp(24.0f) - iconSize, row.h };
            std::u32string name = muiDecodeGameText(gameDialogPartyControlSettingName(setting));
            std::u32string value = muiDecodeGameText(view.settings[setting] != -1
                    ? gameDialogPartyControlValueName(setting, view.settings[setting])
                    : gameDialogPartyControlNotApplicable());
            float nameWidth = std::min(muiTextWidth(name, size), text.w * 0.45f);
            muiDrawTextAligned(name, { text.x, text.y, nameWidth, text.h }, size, theme.text, MuiAlign::Start, MuiAlign::Center);
            MuiRect valueRect = { text.x + nameWidth + ui.dp(8.0f), text.y, text.w - nameWidth - ui.dp(8.0f), text.h };
            float valueSize = muiFitTextSize(value, valueRect.w, size, ui.dp(10.0f));
            muiDrawTextAligned(value, valueRect, valueSize, theme.accent, MuiAlign::End, MuiAlign::Center);

            if (tapped && !scrolling) {
                _gsound_red_butt_press(-1, 0);

                std::vector<MuiActionItem> items;
                int count = gameDialogPartyControlValueCount(setting);
                for (int value = 0; value < count; value++) {
                    MuiActionItem item;
                    item.icon = kPartySettingIcons[setting];
                    item.label = muiDecodeGameText(gameDialogPartyControlValueName(setting, value));
                    item.detail = value == view.settings[setting] ? U"\u2022" : U"";
                    item.enabled = gameDialogPartyControlValueSupported(setting, value);
                    items.push_back(item);
                }

                muiShowActionList(
                    "talk.party.values", items, row, row,
                    [setting](int value) {
                        gameDialogQueuePartyControlAction(PartyControlAction::SetSetting, setting, value);
                    },
                    []() { return gTalkScreen != nullptr && gTalkScreen->partyOpen && gameDialogIsTalking(); });
            }
        }

        muiPopClip();
    }

} // namespace

void muiDialogLayout(MuiContext& ui, MuiRect* content, MuiRect* tabs)
{
    MuiRect safe = ui.safeRect().inset(ui.dp(10.0f));
    float width = ui.dp(52.0f);
    *tabs = { safe.right() - width, safe.y, width, safe.h };
    *content = { safe.x, safe.y, tabs->x - ui.dp(10.0f) - safe.x, safe.h };
}

int muiDialogTabs(MuiContext& ui, const MuiRect& column, MuiDialogTab active, bool barterAvailable, bool partyAvailable)
{
    float size = column.w;
    float gap = ui.dp(8.0f);
    float y = column.y;
    int result = -1;

    struct Tab {
        MuiDialogTab tab;
        const char* id;
        MuiIcon icon;
    };

    const Tab tabs[] = {
        { MuiDialogTab::Talk, "dialog.tab.talk", MuiIcon::Talk },
        { MuiDialogTab::Barter, "dialog.tab.barter", MuiIcon::Barter },
        { MuiDialogTab::Review, "dialog.tab.review", MuiIcon::Review },
        { MuiDialogTab::Party, "dialog.tab.party", MuiIcon::PartyOrders },
    };

    for (const Tab& tab : tabs) {
        if ((tab.tab == MuiDialogTab::Barter && !barterAvailable) || (tab.tab == MuiDialogTab::Party && !partyAvailable)) {
            continue;
        }

        MuiRect rect = { column.x, y, size, size };
        y += size + gap;

        if (muiTabButton(ui, tab.id, rect, tab.icon, tab.tab == active)) {
            result = static_cast<int>(tab.tab);
        }
    }

    return result;
}

int muiSectionList(MuiContext& ui, const std::string& idPrefix, const MuiRect& column, const std::vector<std::u32string>& labels, int selected, const std::vector<bool>& marked)
{
    const MuiTheme& theme = muiTheme();
    ui.panel(column);

    MuiRect inner = column.inset(ui.dp(6.0f));
    float gap = ui.dp(4.0f);
    float height = std::min(ui.dp(46.0f), (inner.h - gap * (labels.size() - 1)) / std::max<size_t>(labels.size(), 1));
    float radius = ui.dp(theme.radius);

    int result = selected;
    for (size_t index = 0; index < labels.size(); index++) {
        MuiRect rect = { inner.x, inner.y + (height + gap) * index, inner.w, height };
        bool on = static_cast<int>(index) == selected;
        bool pressed;
        if (ui.touchable(idPrefix + "." + std::to_string(index), rect, &pressed) && !on) {
            _gsound_red_butt_press(-1, 0);
            result = static_cast<int>(index);
        }

        if (on || pressed) {
            muiFillRoundRect(rect, radius, on ? theme.buttonPrimary : theme.buttonPressed);
        }

        float padding = ui.dp(12.0f);
        float textX = rect.x + padding;
        if (index < marked.size() && marked[index]) {
            float dot = ui.dp(4.0f);
            muiFillCircle(textX + dot, rect.centerY(), dot, on ? theme.buttonPrimaryText : kSectionMarkColor);
            textX += dot * 2.0f + ui.dp(8.0f);
        }

        MuiRect textRect = { textX, rect.y, rect.right() - textX - padding, rect.h };
        float size = muiFitTextSize(labels[index], textRect.w, ui.dp(15.0f), ui.dp(11.0f));
        muiDrawTextAligned(labels[index], textRect, size, on ? theme.buttonPrimaryText : theme.text, MuiAlign::Start, MuiAlign::Center);
    }

    return result;
}

MuiGameScreenTab muiGameScreenTabs(MuiContext& ui, const MuiRect& column, const std::string& idPrefix, MuiGameScreenTab active, bool screens)
{
    struct Tab {
        MuiGameScreenTab tab;
        const char* id;
        MuiIcon icon;
    };

    const Tab tabs[] = {
        { MuiGameScreenTab::Back, "back", MuiIcon::Back },
        { MuiGameScreenTab::Inventory, "inventory", MuiIcon::Inventory },
        { MuiGameScreenTab::Character, "character", MuiIcon::Character },
        { MuiGameScreenTab::Pipboy, "pipboy", MuiIcon::Pipboy },
        { MuiGameScreenTab::Map, "map", MuiIcon::Map },
    };

    float size = column.w;
    float gap = ui.dp(8.0f);
    MuiRect rect = { column.x, column.y, size, size };
    MuiGameScreenTab result = MuiGameScreenTab::None;

    for (const Tab& tab : tabs) {
        if (tab.tab != MuiGameScreenTab::Back && !screens) {
            break;
        }

        // Game screens play the game's sound when they open.
        if (muiTabButton(ui, idPrefix + "." + tab.id, rect, tab.icon, tab.tab == active, tab.tab == MuiGameScreenTab::Back)) {
            result = tab.tab;
        }

        // In combat: what opening the inventory costs; no Pip-Boy in combat
        // or before the vault suit. The overlay is visual only; blocked taps
        // still show the game's message.
        if (tab.tab == MuiGameScreenTab::Inventory && tab.tab != active) {
            std::u32string cost;
            bool blocked = false;
            if (muiInventoryCost(&cost, &blocked)) {
                if (blocked) {
                    muiDrawUnavailableOverlay(ui, rect, muiTheme().radius);
                }
                muiDrawCostBadge(ui, rect, cost, blocked);
            }
        } else if (tab.tab == MuiGameScreenTab::Pipboy && tab.tab != active && pipboyUnavailableReason() != PipboyUnavailable::None) {
            muiDrawUnavailableOverlay(ui, rect, muiTheme().radius);
        }
        rect.y += size + gap;
    }

    return result;
}

bool muiSwitchGameScreen(MuiContext& ui, MuiGameScreenTab tab)
{
    GameCommandType command;

    switch (tab) {
    case MuiGameScreenTab::Inventory: {
        int actionPoints;
        switch (inventoryCheckOpen(gDude, &actionPoints)) {
        case InventoryOpenCheck::NotYourTurn:
            return false;
        case InventoryOpenCheck::NoActionPoints:
            inventoryReportNoActionPoints();
            return false;
        case InventoryOpenCheck::Ok:
            break;
        }
        command = GameCommandType::Inventory;
        break;
    }
    case MuiGameScreenTab::Character:
        command = GameCommandType::Character;
        break;
    case MuiGameScreenTab::Pipboy: {
        // The screen stays, the game's message says why.
        PipboyUnavailable reason = pipboyUnavailableReason();
        if (reason != PipboyUnavailable::None) {
            if (reason == PipboyUnavailable::InCombat) {
                soundPlayFile("iisxxxx1");
            }
            muiNotify(pipboyUnavailableText(reason), MuiNoticeKind::Warning);
            return false;
        }
        command = GameCommandType::Pipboy;
        break;
    }
    case MuiGameScreenTab::Map:
        command = GameCommandType::Automap;
        break;
    default:
        return false;
    }

    gameCommandPost(command);
    muiHoldFrameForSwitch(ui.current);
    return true;
}

bool muiInventoryCost(std::u32string* text, bool* blocked)
{
    int actionPoints;
    InventoryOpenCheck check = inventoryCheckOpen(gDude, &actionPoints);
    if (check == InventoryOpenCheck::NotYourTurn || actionPoints <= 0) {
        return false;
    }

    std::string value = std::to_string(actionPoints);
    *text = std::u32string(value.begin(), value.end());
    *blocked = check == InventoryOpenCheck::NoActionPoints;
    return true;
}

void muiDrawCostBadge(MuiContext& ui, const MuiRect& button, const std::u32string& text, bool warning)
{
    const MuiTheme& theme = muiTheme();
    float size = ui.dp(11.0f);
    float height = ui.dp(17.0f);
    float width = std::max(height, muiTextWidth(text, size) + ui.dp(8.0f));
    MuiRect badge = { button.right() - width + ui.dp(3.0f), button.y - ui.dp(3.0f), width, height };
    MuiColor color = warning ? muiRgb(0xFF6E5E) : theme.accent;
    muiFillRoundRect(badge, height / 2.0f, muiRgb(0x0B120D));
    muiStrokeRoundRect(badge, height / 2.0f, ui.dp(1.2f), color);
    muiDrawTextAligned(text, badge, size, color, MuiAlign::Center, MuiAlign::Center);
}

void muiDrawUnavailableOverlay(MuiContext& ui, const MuiRect& button, float radiusDp)
{
    muiFillRoundRect(button, ui.dp(radiusDp), muiRgb(0x000000, 140));
}

bool muiTabButton(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiIcon icon, bool active, bool sound)
{
    const MuiTheme& theme = muiTheme();

    bool pressed;
    bool tapped = ui.touchable(id, rect, &pressed);
    MuiColor fill = pressed ? theme.buttonPressed : (active ? theme.buttonPrimary : theme.button);
    MuiColor iconColor = active && !pressed ? theme.buttonPrimaryText : theme.accent;
    muiFillRoundRect(rect, ui.dp(theme.radius), fill);
    muiStrokeRoundRect(rect, ui.dp(theme.radius), ui.dp(theme.borderWidth), pressed || active ? theme.accent : theme.buttonBorder);
    muiDrawIcon(icon, rect.centerX(), rect.centerY(), rect.w * 0.46f, ui.dp(2.2f), iconColor);

    if (tapped && !active) {
        if (sound) {
            _gsound_red_butt_press(-1, 0);
        }
        return true;
    }
    return false;
}

void muiGameDialogOpenReview()
{
    if (gTalkScreen != nullptr) {
        gTalkScreen->reviewPending = true;
    }
}

void muiGameDialogOpenParty()
{
    if (gTalkScreen != nullptr) {
        gTalkScreen->partyPending = true;
    }
}

void muiGameDialogInit()
{
    if (!muiIsEnabled() || gTalkScreen != nullptr) {
        return;
    }

    gTalkScreen = new TalkScreen();
    muiPush(gTalkScreen);
}

void muiGameDialogExit()
{
    if (gTalkScreen == nullptr) {
        return;
    }

    muiRemove(gTalkScreen);
    delete gTalkScreen;
    gTalkScreen = nullptr;
}

bool muiGameDialogBack()
{
    if (gTalkScreen == nullptr || (!gTalkScreen->reviewOpen && !gTalkScreen->partyOpen)) {
        return false;
    }

    gTalkScreen->reviewOpen = false;
    gTalkScreen->partyOpen = false;
    return true;
}

} // namespace fallout
