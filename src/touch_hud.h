#ifndef FALLOUT_TOUCH_HUD_H_
#define FALLOUT_TOUCH_HUD_H_

#include "hud_layout.h"
#include "interface.h"
#include "mui_draw.h"
#include "touch.h"

namespace fallout {

// Touch HUD: replaces the original interface bar with groups of controls
// attached to screen corners and edges (layout in hud_layout.cc), drawn by
// the mobile UI (mui.h). Hidden under game screens.
//
// The original bar keeps its state and logic (item states, attack modes, AP,
// end buttons, indicators, display monitor) without its window
// (`interfaceBarWindowless`); HUD shows the same state and sends the same
// commands as the bar buttons.

// `[touch] hud` setting. Always on in the touch-only build (touch.h), a
// constant there: the original bar's own drawing is left out of it.
#if FALLOUT_TOUCH_ONLY
constexpr bool touchHudIsEnabled() { return true; }
#else
bool touchHudIsEnabled();
#endif

// Item highlight (HUD toggle) is on.
bool touchHudIsHighlightActive();

// The object's outline is the highlight button's (see touch_hud.cc).
bool touchHudHighlightOwns(Object* object);

void touchHudInit();
void touchHudFree();
void touchHudShow();
void touchHudHide();
void touchHudSetEnabled(bool enabled);

// State change notifications from interface.cc and display_monitor.cc.
void touchHudSetActionPoints(int actionPoints, int bonusActionPoints, int maxActionPoints);
void touchHudSetCombatButtons(bool visible, bool enabled);
void touchHudAddMessage(const char* message);
void touchHudClearMessages();

// Runs the HUD's own action `eventCode` (`GameCommandType::Hud`) in the game
// loop, `false` - not one of them.
bool touchHudHandleEvent(int eventCode);

// Indicators (SNEAK, LEVEL, ...) are shown by HUD instead of the bar.
void touchHudSetIndicators(const InterfaceIndicator* indicators, int count);

// Screen center of HUD element / attack mode chip (for automated tests).
bool touchHudGetElementScreenCenter(HudElementId id, int* x, int* y);
// Rect of element [id] of the last frame in output pixels (mobile UI).
bool touchHudGetElementRect(HudElementId id, MuiRect* rect);
bool touchHudGetModeScreenCenter(int index, int* x, int* y);

} // namespace fallout

#endif /* FALLOUT_TOUCH_HUD_H_ */
