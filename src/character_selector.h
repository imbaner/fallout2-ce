#ifndef CHARACTER_SELECTOR_H
#define CHARACTER_SELECTOR_H

#include <string>
#include <vector>

#include "art_defs.h"

namespace fallout {

// 2 - a character was taken or made, 3 - back.
int characterSelectorOpen();

// Premade characters as the selector shows them (the game's three or
// `[characters]` of the content config), for the mobile selector
// (mui_character_selector.cc).
int premadeCharacterCount();
// Shown one (kept between openings, as the game's window).
int premadeCharacterSelected();
void premadeCharacterSelect(int index);
// Makes the dude premade character [index] (its .gcd), as the game's window
// does when it shows it.
bool premadeCharacterLoad(int index);
// Its portrait (interface art, 592x260).
InterfaceFrameId premadeCharacterFace(int index);
// Its biography (.bio): lines of the file (game text).
std::vector<std::string> premadeCharacterBio(int index);

void premadeCharactersInit();
void premadeCharactersExit();

} // namespace fallout

#endif /* CHARACTER_SELECTOR_H */
