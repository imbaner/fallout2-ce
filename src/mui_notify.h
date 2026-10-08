#ifndef FALLOUT_MUI_NOTIFY_H_
#define FALLOUT_MUI_NOTIFY_H_

#include "mui.h"

namespace fallout {

// Notifications of the mobile UI: what the player should see right away,
// wherever they are. One queue for every source, shown where it's seen:
// - on the map with the touch HUD: next to the HUD's log button (like the
//   game's message log under the interface bar);
// - over screens covering the HUD (inventory, talk, barter, character, the
//   game's full screen windows): plates at the top center.
//
// Sources feed it where the game itself shows them, the game's logic stays:
// the message log (`displayMonitorAddMessage`), replies of the one talked to
// (`gameDialogRenderSupplementaryMessage`: barter, party member control),
// mistakes on the mobile screens.

enum class MuiNoticeKind {
    // The game's message log.
    Log,
    // What the one talked to replies (with their name).
    Reply,
    // Mistake on a screen (not possible, not enough).
    Warning,
};

// [text] and [title] are in the game's charset.
void muiNotify(const char* text, MuiNoticeKind kind, const char* title = nullptr);

// Forgets notifications not shown yet (the message log is cleared: new game,
// load).
void muiNotifyClear();

// Touch HUD: shows log notifications next to its log button this frame in
// [area] (bottom aligned), or none while [logOpen] (the log shows them).
void muiNotifyHudArea(const MuiRect& area, bool logOpen);

// mui.cc: draws notifications over every screen.
void muiNotifyRender(MuiContext& ui);

} // namespace fallout

#endif /* FALLOUT_MUI_NOTIFY_H_ */
