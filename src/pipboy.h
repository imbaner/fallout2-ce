#ifndef PIPBOY_H
#define PIPBOY_H

#include <string>
#include <vector>

#include "db.h"
#include "map_defs.h"
#include "message.h"

namespace fallout {

typedef enum PipboyOpenIntent {
    PIPBOY_OPEN_INTENT_UNSPECIFIED = 0,
    PIPBOY_OPEN_INTENT_REST = 1,
} PipboyOpenIntent;

int pipboyOpen(int intent);

// CE: Why the Pip-Boy can't open now, by the game's rules (`gameHandleKey`
// and `pipboyOpen`), as `inventoryCheckOpen` for the inventory: the touch
// HUD and the game screens' tabs show it unavailable; the tabs say why
// instead of leaving the screen.
enum class PipboyUnavailable {
    None,
    // "Pip-Boy not available in combat!"
    InCombat,
    // "You aren't wearing the pipboy!": the game's start, before the vault
    // suit.
    NotWorn,
};

PipboyUnavailable pipboyUnavailableReason();

// The game's text for [reason] (misc.msg).
const char* pipboyUnavailableText(PipboyUnavailable reason);

// As the game tells it (the map, the HUD's button): its sound and dialog
// box.
void pipboyShowUnavailable(PipboyUnavailable reason);
void pipboyInit();
void pipboyReset();
void pipboySetRestHealTime(int minutes);
int pipboySave(File* stream);
int pipboyLoad(File* stream);
int pipboyGetWindow();
bool pipboyIsResting();
bool pipboyRestOptionMsgsSetBase(int baseMessageId);
bool pipboyRestOptionSet(int restOption, int value);

extern MessageList gPipboyMessageList;
int pipboyMessageListInit();
void pipboyMessageListFree();

// CE: Mobile UI Pip-Boy screen (mui_pipboy.cc) works over the Pip-Boy's
// state while `pipboyOpen` runs: the same data and rules as the game's
// window, which isn't created then. Texts are in the game's charset.

// Text [id] of `game\pipboy.msg`.
const char* pipboyGetText(int id);

// Greeting of today's holiday, nullptr on other days.
const char* pipboyGetHoliday();

struct PipboyQuest {
    const char* text;
    bool completed;
};

struct PipboyQuestLocation {
    const char* name;
    std::vector<PipboyQuest> quests;
    int activeCount = 0;
};

// Quests shown now (global variable thresholds), by location.
std::vector<PipboyQuestLocation> pipboyGetQuests();

struct PipboyHolodisk {
    int index;
    const char* name;
};

std::vector<PipboyHolodisk> pipboyGetHolodisks();

// Paragraphs of holodisk [index] (its lines joined).
std::vector<std::string> pipboyGetHolodiskText(int index);

// Plays the voiced narration of holodisk [index] if it has one (stops the
// one playing); `pipboySoundStop` stops it.
void pipboyPlayHolodiskNarration(int index);

struct PipboyAutomap {
    Map map;
    int elevation;
    const char* name;
};

struct PipboyAutomapLocation {
    const char* name;
    std::vector<PipboyAutomap> maps;
};

// Locations with saved automaps and their maps / elevations.
std::vector<PipboyAutomapLocation> pipboyGetAutomaps();

struct PipboyVideo {
    int movie;
    const char* name;
};

// Movies seen.
std::vector<PipboyVideo> pipboyGetVideos();

typedef enum PipboyRestDuration {
    PIPBOY_REST_DURATION_TEN_MINUTES,
    PIPBOY_REST_DURATION_THIRTY_MINUTES,
    PIPBOY_REST_DURATION_ONE_HOUR,
    PIPBOY_REST_DURATION_TWO_HOURS,
    PIPBOY_REST_DURATION_THREE_HOURS,
    PIPBOY_REST_DURATION_FOUR_HOURS,
    PIPBOY_REST_DURATION_FIVE_HOURS,
    PIPBOY_REST_DURATION_SIX_HOURS,
    PIPBOY_REST_DURATION_UNTIL_MORNING,
    PIPBOY_REST_DURATION_UNTIL_NOON,
    PIPBOY_REST_DURATION_UNTIL_EVENING,
    PIPBOY_REST_DURATION_UNTIL_MIDNIGHT,
    PIPBOY_REST_DURATION_UNTIL_HEALED,
    PIPBOY_REST_DURATION_UNTIL_PARTY_HEALED,
    PIPBOY_REST_DURATION_COUNT,
    PIPBOY_REST_DURATION_COUNT_WITHOUT_PARTY = PIPBOY_REST_DURATION_COUNT - 1,
} PipboyRestDuration;

// Rest (alarm clock): options (`PipboyRestDuration`, the last one only with
// a party to heal), their texts and wake hours (until ... options).
bool pipboyCanRest();
int pipboyGetRestOptionCount();
const char* pipboyGetRestOptionText(int option);
int pipboyGetRestOptionWakeHour(int option);

// Rests for [option] (runs the game's rest with its clock, healing, events;
// Esc or `pipboyStopRest` stops it). Returns true when the Pip-Boy has to
// close (something happened).
bool pipboyRestFor(int option);

// Stops the rest going on (as Esc does).
void pipboyStopRest();

// Something happened while resting: the Pip-Boy closes.
bool pipboyShouldClose();

} // namespace fallout

#endif /* PIPBOY_H */
