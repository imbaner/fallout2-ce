#ifndef FALLOUT_PREFERENCES_H_
#define FALLOUT_PREFERENCES_H_

#include "db.h"

namespace fallout {

int preferencesInit();
int doPreferences(bool animated);

// CE: The preferences of the game's window, for the mobile settings screen
// (mui_preferences.cc). These are the values the game uses (also kept in
// saves), in the window's units.
struct PreferenceValues {
    int gameDifficulty;
    int combatDifficulty;
    int violenceLevel;
    int targetHighlight;
    int combatLooks;
    int combatMessages;
    int combatTaunts;
    int languageFilter;
    int running;
    int subtitles;
    int itemHighlight;
    int combatSpeed;
    int playerSpeedup;
    // Seconds, 1 (fast) - 6 (slow).
    double textBaseDelay;
    int masterVolume;
    int musicVolume;
    int soundEffectsVolume;
    int speechVolume;
    // 1.0 - 1.18.
    double brightness;
    double mouseSensitivity;

    bool operator==(const PreferenceValues& other) const;
    bool operator!=(const PreferenceValues& other) const { return !(*this == other); }
};

PreferenceValues preferencesGetValues();
// The window's Default button.
PreferenceValues preferencesGetDefaults();
// Volumes and brightness of [values] heard and seen now, nothing kept (the
// window does this while a knob turns).
void preferencesPreview(const PreferenceValues& values);
// Takes [values] as the window's Done does: applied, written to
// fallout2.cfg (with the rest of the settings).
void preferencesApply(const PreferenceValues& values);
// The window's samples while a volume changes: an effect, the narrator.
void preferencesPlaySoundEffectSample();
void preferencesPlaySpeechSample();
int preferencesSave(File* stream);
int preferencesLoad(File* stream);
void brightnessIncrease();
void brightnessDecrease();

} // namespace fallout

#endif /* FALLOUT_PREFERENCES_H_ */
