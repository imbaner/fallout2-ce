#ifndef CHARACTER_EDITOR_H
#define CHARACTER_EDITOR_H

#include <string>
#include <vector>

#include "art.h"
#include "db.h"
#include "perk_defs.h"
#include "skill_defs.h"
#include "stat_defs.h"
#include "trait_defs.h"

namespace fallout {

extern int gCharacterEditorRemainingCharacterPoints;

enum PerkCarryOverMode {
    // Unspent perks are lost on level up (vanilla)
    PERK_CARRY_OVER_MODE_OFF = 0,
    // Unspent perks are preserved on level up, but you still spend unspent perks at the appropriate levels.
    // E.g. if you reach level 9 and have never spent any perks, you first choose from the level 3 perks, then 6, then 9. (CE)
    PERK_CARRY_OVER_MODE_ON = 1,
    // Unspent perks are preserved on level up, and you can choose to spend them on perks for the current level.
    PERK_CARRY_OVER_MODE_SFALL = 2,
};

int characterEditorShow(bool isCreationMode);
void characterEditorInit();
bool _isdoschar(int ch);
char* _strmfe(char* dest, const char* name, const char* ext);
int characterEditorSave(File* stream);
int characterEditorLoad(File* stream);
void characterEditorReset();
int characterEditorGetWindow();
void characterEditorDisplayStats();
int characterEditorGetPerkOwed();
void characterEditorSetPerkOwed(int value);
void characterEditorSetPerkFrequency(int value);
void characterEditorSetSkillPointsPerLevelModifier(int value);
void characterEditorHandleLevelUp(int level);
int characterEditorGetPerkSelectionLevel();
const std::vector<int>& characterEditorGetOwedPerkLevels();
bool characterEditorSetOwedPerkLevels(const std::vector<int>& levels);
void characterEditorMigrateLegacyPerkSelectionState();

// CE: Mobile UI character screen (mui_character.cc) works over the editor's
// state while `characterEditorShow` runs: the same rules and texts as the
// game's window, which isn't created then.

// Description of an item: skilldex picture, title, description (game
// texts).
struct CharacterEditorCard {
    SkillDexFrmId frmId;
    std::string title;
    // Next to the title (skills: base value and attributes).
    std::string subtitle;
    std::string description;
};

enum class CharacterEditorFolder {
    Perks,
    Karma,
    Kills,
};

// Line of the perks, karma or kills list.
struct CharacterEditorFolderEntry {
    std::string text;
    bool heading = false;
    // Perks with several ranks.
    int rank = 0;
    int maxRank = 0;
    // Kills.
    int count = -1;
    bool hasCard = false;
    CharacterEditorCard card;
};

// Conditions of the status list (poisoned, radiated, eye damage, crippled
// limbs).
constexpr int kCharacterEditorConditionCount = 7;

// Derived stats of the stats list (armor class ... critical chance).
constexpr int kCharacterEditorDerivedStatCount = 10;

enum class CharacterEditorSkillChange {
    Changed,
    NoPoints,
    AtMaximum,
    AtMinimum,
};

// Next step after a perk is chosen: some perks ask for more.
enum class CharacterEditorPerkStep {
    Done,
    // Tag! - one more tag skill.
    TagSkill,
    // Mutate! - a trait to lose, then a new one.
    LoseTrait,
    NewTrait,
    Failed,
};

bool characterEditorIsCreating();

// Text [id] of `game\editor.msg`.
const char* characterEditorGetText(int id);

// Creation: character points left, tag skills left, traits left.
int characterEditorGetCharacterPoints();
int characterEditorGetTagSkillsLeft();
int characterEditorGetTraitsLeft();

// Primary stat as the list shows it (creation: base with traits and bonus)
// and its description (Average, Good...).
int characterEditorGetPrimaryStat(Stat stat);
const char* characterEditorGetPrimaryStatDescription(Stat stat);
bool characterEditorAdjustPrimaryStat(Stat stat, int delta);

bool characterEditorIsSkillTagged(Skill skill);
// False when all tag skills are used (message 140, 141).
bool characterEditorToggleTagSkill(Skill skill);

bool characterEditorIsTraitSelected(Trait trait);
// False when both traits are taken (message 148, 149).
bool characterEditorToggleTrait(Trait trait);

// Skill points (not in creation).
CharacterEditorSkillChange characterEditorAdjustSkill(Skill skill, int delta);
bool characterEditorCanLowerSkill(Skill skill);

// Status and derived stats list.
void characterEditorGetHitPoints(int* current, int* maximum);
bool characterEditorConditionIsActive(int condition);
const char* characterEditorGetConditionName(int condition);
const char* characterEditorGetDerivedStatName(int index);
std::string characterEditorFormatDerivedStat(int index, bool* warning);

// Cards of the lists.
CharacterEditorCard characterEditorGetPrimaryStatCard(Stat stat);
CharacterEditorCard characterEditorGetDerivedStatCard(int index);
CharacterEditorCard characterEditorGetHitPointsCard();
CharacterEditorCard characterEditorGetConditionCard(int condition);
CharacterEditorCard characterEditorGetSkillCard(Skill skill);
CharacterEditorCard characterEditorGetTraitCard(Trait trait);
CharacterEditorCard characterEditorGetPerkCard(Perk perk);
// Level, experience, next level (0-2); creation: character points.
CharacterEditorCard characterEditorGetLevelCard(int line);
// Skill points (tag skills in creation), traits.
CharacterEditorCard characterEditorGetSkillPointsCard();
CharacterEditorCard characterEditorGetTraitsCard();

std::vector<CharacterEditorFolderEntry> characterEditorGetFolder(CharacterEditorFolder folder);
// Card of the list itself (nothing chosen).
CharacterEditorCard characterEditorGetFolderCard(CharacterEditorFolder folder);

// Something changed since the screen opened (skill points, perks), which
// `characterEditorRevert` puts back.
bool characterEditorHasChanges();
void characterEditorRevert();

// Creation: checks before starting the game; returns the first line of the
// message (the second is the next id) or 0 when ready.
int characterEditorCheckReady();
// Name wasn't changed: message 160, 161 asks to use it anyway.
bool characterEditorNameIsDefault();

// Creation: name, age, gender.
void characterEditorSetName(const char* name);
bool characterEditorAdjustAge(int delta);
void characterEditorSetGender(int gender);

// Perk choice for owed perks (`characterEditorGetPerkOwed`): perks available
// now (sorted by name), then the choice and its extra steps.
std::vector<Perk> characterEditorGetAvailablePerks();
CharacterEditorPerkStep characterEditorChoosePerk(Perk perk);
std::vector<Skill> characterEditorGetNewTagSkillOptions();
std::vector<Trait> characterEditorGetLoseTraitOptions();
std::vector<Trait> characterEditorGetNewTraitOptions();
CharacterEditorPerkStep characterEditorChooseTagSkill(Skill skill);
CharacterEditorPerkStep characterEditorChooseLoseTrait(Trait trait);
CharacterEditorPerkStep characterEditorChooseNewTrait(Trait trait);
// Takes back the perk waiting for its extra steps.
void characterEditorCancelPerk();

} // namespace fallout

#endif /* CHARACTER_EDITOR_H */
