#ifndef GAME_DIALOG_H
#define GAME_DIALOG_H

#include "art.h"
#include "interpreter.h"
#include "obj_types.h"
#include "skill_defs.h"

namespace fallout {

extern Object* gGameDialogSpeaker;
extern bool gGameDialogSpeakerIsPartyMember;
extern HeadFrmId gGameDialogHeadFrmId;
extern int gGameDialogSid;

int gameDialogInit();
int gameDialogReset();
int gameDialogExit();
bool _gdialogActive();
void gameDialogEnter(Object* speaker, int mode);
void _gdialogSystemEnter();
void gameDialogStartLips(const char* audioFileName);
int gameDialogEnable();
int gameDialogDisable();
int _gdialogInitFromScript(const HeadFrmId& headFrmId, HeadFidget reaction);
int _gdialogExitFromScript();
void gameDialogSetBackground(const BackgroundFrmId& background);
void gameDialogRenderSupplementaryMessage(const char* msg);
int _gdialogStart();
int _gdialogSayMessage();
int gameDialogAddMessageOptionWithProcIdentifier(int messageListId, int messageId, const char* procName, int reaction);
int gameDialogAddTextOptionWithProcIdentifier(int messageListId, const char* text, const char* procName, int reaction);
int gameDialogAddMessageOptionWithProc(int messageListId, int messageId, int proc, int reaction);
int gameDialogAddTextOptionWithProc(int messageListId, const char* text, int proc, int reaction);
int gameDialogSetMessageReply(Program* program, int messageListId, int messageId);
int gameDialogSetTextReply(Program* program, int messageListId, const char* text);
int _gdialogGo();
void _gdialogUpdatePartyStatus();
void _talk_to_critter_reacts(int reaction);
int gameDialogGetBarterModifier();
void gameDialogSetBarterModifier(int modifier);
int gameDialogBarter(int modifier);
void gameDialogEndBarter();
bool gameDialogIsBarterWindowExpanded();
int gameDialogGetWindow();
int gameDialogGetBackgroundWindow();
void gameDialogSetPartyMemberCcMsgIds(int pid, int startMsgId, int endMsgId);
void gameDialogResetPartyMemberCcMsgIds();

// CE: Talk screen data for the mobile UI (mui_game_dialog.cc).

// Party member control on the mobile UI talk screen: the data and actions of
// the game's combat control and customization panels, with the same rules
// (options a party member can't use come from data\party.txt).
constexpr int kPartyControlDispositionCount = 5;
constexpr int kPartyControlSettingCount = 6;

struct PartyControlView {
    Object* weapon;
    Object* armor;
    int hitPoints;
    int maximumHitPoints;
    Skill bestSkill;
    int weight;
    int carryWeight;
    bool encumbered;
    int meleeDamage;
    int actionPoints;
    int maximumActionPoints;
    // sfall's PartyMemberExtraInfo (ddraw.ini).
    bool extraInfo;
    int level;
    int armorClass;
    bool addicted;
    bool canEquipArmor;
    // 0 custom, 1 coward, 2 defensive, 3 aggressive, 4 berserk (the game's
    // disposition buttons).
    int disposition;
    bool dispositionSupported[kPartyControlDispositionCount];
    // Index of each setting's current value, -1 - not applicable.
    int settings[kPartyControlSettingCount];
};

enum class PartyControlAction {
    UseBestWeapon,
    UseBestArmor,
    // a - disposition.
    SetDisposition,
    // a - setting, b - value; switches to the custom disposition first, as
    // the game's customization panel is only reached through it.
    SetSetting,
};

// The speaker is a party member: the control tab is shown.
bool gameDialogSpeakerIsPartyMember();
bool gameDialogGetPartyControlView(PartyControlView* view);
// Setting names and values, `game\custom.msg`.
const char* gameDialogPartyControlSettingName(int setting);
int gameDialogPartyControlValueCount(int setting);
const char* gameDialogPartyControlValueName(int setting, int value);
const char* gameDialogPartyControlNotApplicable();
bool gameDialogPartyControlValueSupported(int setting, int value);
// Performed by the talk loop (it checks them every frame).
void gameDialogQueuePartyControlAction(PartyControlAction action, int a = 0, int b = 0);

// The talk loop chooses option [index] (any option, digit keys reach only
// the first nine).
void gameDialogChooseOption(int index);

// The talk loop opens barter (the game's barter button).
void gameDialogRequestBarter();

// Talking screen is shown (not barter, party control, review or a
// transition).
bool gameDialogIsTalking();
// Barter window is being opened or closed, or barter runs.
bool gameDialogIsInBarter();
const char* gameDialogGetReplyText();
int gameDialogGetOptionCount();
const char* gameDialogGetOptionText(int index);
// Reaction shown with Empathy perk: 0 - good, 1 - neutral, 2 - bad; -1
// without the perk.
int gameDialogGetOptionReaction(int index);
int gameDialogGetReviewCount();
// [option] is nullptr when the reply had no answer.
void gameDialogGetReviewEntry(int index, const char** reply, const char** option);
// Talking head / map around the speaker as drawn by the game.
bool gameDialogGetHeadImage(const unsigned char** data, int* width, int* height, int* pitch);
bool gameDialogSpeakerCanBarter();

} // namespace fallout

#endif /* GAME_DIALOG_H */
