#include "dev_autotest.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <lodepng.h>

#include "actions.h"
#include "art.h"
#include "animation.h"
#include "character_editor.h"
#include "combat.h"
#include "critter.h"
#include "db.h"
#include "elevator.h"
#include "game.h"
#include "game_commands.h"
#include "game_dialog.h"
#include "game_movie.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "interface.h"
#include "inventory.h"
#include "loadsave.h"
#include "map_hints.h"
#include "map.h"
#include "movie.h"
#include "scripts.h"
#include "platform_compat.h"
#include "save_storage.h"
#include "input.h"
#include "item.h"
#include "kb.h"
#include "mouse.h"
#include "mui.h"
#include "object.h"
#include "party_member.h"
#include "perf_monitor.h"
#include "perk.h"
#include "player_commands.h"
#include "preferences.h"
#include "settings.h"
#include "skill.h"
#include "sound.h"
#include "stat.h"
#include "svga.h"
#include "text_object.h"
#include "tile.h"
#include "touch_controls.h"
#include "touch.h"
#include "touch_hud.h"
#include "touch_log.h"
#include "window_manager.h"
#include "world_view.h"
#include "worldmap.h"

namespace fallout {

enum DevAutotestAction {
    DEV_AUTOTEST_ACTION_NONE,
    DEV_AUTOTEST_ACTION_ZOOM,
    DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE,
    DEV_AUTOTEST_ACTION_PAN,
    // Synthetic two finger touch: a - start span, b - end span, c -
    // horizontal centroid movement (all in fractions of window width).
    DEV_AUTOTEST_ACTION_PINCH,
    // Points mouse at the tile `a` hexes away from dude and walks there the
    // same way as mouse click does.
    DEV_AUTOTEST_ACTION_WALK,
    // Switches game mouse to arrow mode with mouse at the given screen point.
    DEV_AUTOTEST_ACTION_ARROW,
    // Logs where dude ended up after the walk.
    DEV_AUTOTEST_ACTION_CHECK_WALK,
    // Checks that a map tap after title-screen load moved the player.
    DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED,
    // Centers view on dude.
    DEV_AUTOTEST_ACTION_CENTER_DUDE,
    // Synthetic single finger touches (touch controls):
    // drag from view center by (a, b) screen pixels;
    DEV_AUTOTEST_ACTION_TOUCH_DRAG,
    // tap on the tile `a` hexes away from dude in direction `b`;
    DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE,
    // long press on the nearest critter;
    DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER,
    // tap on radial menu button of action menu item `a`;
    DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL,
    // tap on the nearest critter.
    DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER,
    // Attacks the nearest critter with mouse (starts combat, the script
    // continues from combat loop during player turns).
    DEV_AUTOTEST_ACTION_START_COMBAT,
    // Logs combat state.
    DEV_AUTOTEST_ACTION_LOG_COMBAT,
    // Tap on the tile next to the nearest critter (on dude's side).
    DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_CRITTER,
    // Gives dude item with pid `a`.
    DEV_AUTOTEST_ACTION_GIVE_ITEM,
    // Presses key with SDL scancode `a` (desktop screens; the touch-only
    // build takes only a text field's editing keys).
    DEV_AUTOTEST_ACTION_KEY,
    // Posts game command `a` (`GameCommandType`), as the HUD's buttons do.
    DEV_AUTOTEST_ACTION_COMMAND,
    // Back (Android's back button).
    DEV_AUTOTEST_ACTION_BACK,
    // Logs the talk: reply, options, dialog modes, caps and inventory sizes
    // of dude and the speaker, checksums of global, map and the speaker's
    // local variables (one run of the game's logic gives the same lines).
    DEV_AUTOTEST_ACTION_LOG_DIALOG,
    // Logs the game's windows made since the scenario started (hidden ones
    // too).
    DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS,
    // Logs the critters at dude's elevation (pid, name, tile, distance).
    DEV_AUTOTEST_ACTION_LOG_CRITTERS,
    // Talks to the critter with pid `0x01000000 + a` (walks there first).
    DEV_AUTOTEST_ACTION_TALK_TO_PID,
    // Chooses talk option `a`: the mobile UI screen's way, or the digit key
    // with the game's windows.
    DEV_AUTOTEST_ACTION_CHOOSE_OPTION,
    // Waits (up to `a` frames) until a talk shows options, then as NONE.
    DEV_AUTOTEST_ACTION_WAIT_TALKING,
    // The talk's barter, barter's offer and back to talk: the mobile UI
    // screens' way, or the game's keys (B, M, T) with its windows.
    DEV_AUTOTEST_ACTION_DIALOG_BARTER,
    DEV_AUTOTEST_ACTION_BARTER_OFFER,
    DEV_AUTOTEST_ACTION_BARTER_TALK,
    // Tap a legacy screen point (a, b), for title-menu buttons.
    DEV_AUTOTEST_ACTION_TOUCH_TAP_SCREEN,
    // Inventory slot touches: tap on slot `a`; long press on slot `a`
    // (captures while held); drag from slot `a` to slot `b`.
    // Logs dude's hands and inventory.
    DEV_AUTOTEST_ACTION_LOG_INVENTORY,
    // Logs whether mod files are visible.
    DEV_AUTOTEST_ACTION_CHECK_MODS,
    // After title-screen Load, the world palette and map input must be ready.
    DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD,
    // Centers view on the nearest critter.
    DEV_AUTOTEST_ACTION_CENTER_CRITTER,
    // Tap on the tile next to dude on the side opposite to the nearest
    // critter.
    DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER,
    // World map: open it (the script continues from its loop); log its
    // state; drag from view center by (a, b); tap at party position + (a, b).
    DEV_AUTOTEST_ACTION_OPEN_WORLDMAP,
    DEV_AUTOTEST_ACTION_LOG_WORLDMAP,
    DEV_AUTOTEST_ACTION_WORLDMAP_DRAG,
    DEV_AUTOTEST_ACTION_WORLDMAP_TAP,
    // Logs attack mode.
    DEV_AUTOTEST_ACTION_LOG_HIT_MODE,
    // Creates item with pid `a` and equips it in hand `b`.
    DEV_AUTOTEST_ACTION_EQUIP_ITEM,
    // Touch HUD: tap on element `a` (HudElementId); tap on attack mode chip
    // `a`.
    DEV_AUTOTEST_ACTION_HUD_TAP,
    DEV_AUTOTEST_ACTION_HUD_TAP_MODE,
    // Same with finger up in the same frame.
    DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE,
    // Logs whether save slot `a` (1-based) has a save.
    DEV_AUTOTEST_ACTION_LOG_SLOT,
    // Tap magnets: tap at destination marker + (a, b) screen pixels; logs
    // dude movement; puts item with pid `a` on the ground `b` hexes from dude
    // (direction `c`); taps at that item + (a, b).
    DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER,
    DEV_AUTOTEST_ACTION_LOG_MOVEMENT,
    DEV_AUTOTEST_ACTION_PLACE_ITEM,
    DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM,
    // Logs whether the placed item is still on the ground.
    DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM,
    // Mobile UI: tap on widget `widget` (a = 1 - quick tap, finger up in the
    // same frame).
    DEV_AUTOTEST_ACTION_MUI_TAP,
    // Mobile UI: drag from widget `widget` to widget `target`.
    DEV_AUTOTEST_ACTION_MUI_DRAG,
    // Holds HUD element `a` for `b` frames, captures "<name>_held" before
    // the finger goes up.
    DEV_AUTOTEST_ACTION_HUD_LONG_PRESS,
    // Mobile UI: holds widget `widget` for `a` frames, captures
    // "<name>_held" before the finger goes up.
    DEV_AUTOTEST_ACTION_MUI_LONG_PRESS,
    // Kills the nearest critter, it's the corpse for the actions below.
    DEV_AUTOTEST_ACTION_KILL_CRITTER,
    // Puts item with pid `a` into the corpse's inventory.
    DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM,
    DEV_AUTOTEST_ACTION_CENTER_CORPSE,
    DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE,
    // Floating text over dude (like float_msg).
    DEV_AUTOTEST_ACTION_FLOAT_TEXT,
    // Critter proto `a` (index, pid is 0x1000000 + a) `b` hexes from dude
    // (direction `c`) with script `widget` (file name in scripts.lst), joins
    // the party; it's the one GIVE_PARTY_ITEM gives to.
    DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER,
    // Item pid `a` x `b` to the last added party member.
    DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM,
    // The party member of `ADD_PARTY_MEMBER` gets items of pid [a] until he
    // can carry only [b] more.
    DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT,
    // The party member of `ADD_PARTY_MEMBER` [a] tiles south-east of the dude.
    DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER,
    // The last added party member takes item pid `a` of its inventory in
    // hand.
    DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM,
    // Dude's name `widget` (game encoding).
    DEV_AUTOTEST_ACTION_SET_NAME,
    // Saves the game to slot `a` (0-based) with description `widget`.
    DEV_AUTOTEST_ACTION_SAVE_GAME,
    DEV_AUTOTEST_ACTION_QUICK_SAVE,
    DEV_AUTOTEST_ACTION_QUICK_LOAD,
    DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION,
    // Loads slot `a` (0-based).
    DEV_AUTOTEST_ACTION_LOAD_GAME,
    // Logs party members.
    DEV_AUTOTEST_ACTION_LOG_PARTY,
    // Ends the turn and [c] frames later drags the map by ([a], [b]) while
    // others act.
    DEV_AUTOTEST_ACTION_PAN_DURING_ENEMY_TURN,
    // Talks to the first party member (the dialog loop runs inside).
    DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER,
    // Taps at ([a], [b]) in the game dialog's lower window (the game's own
    // windows, for screens without the mobile UI).
    DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW,
    // Logs the party member control data of the dialog speaker.
    DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL,
    // Types the text of the widget field (UTF-8).
    DEV_AUTOTEST_ACTION_TYPE_TEXT,
    // Sends event code [a] to the game's input (window buttons).
    DEV_AUTOTEST_ACTION_EVENT,
    // Sets global variable [a] to [b].
    DEV_AUTOTEST_ACTION_SET_GVAR,
    // Plays game movie [a] (the step completes first, the movie loop runs
    // the script on).
    DEV_AUTOTEST_ACTION_PLAY_MOVIE,
    // Requires a decoded movie frame during the full-screen playback loop.
    DEV_AUTOTEST_ACTION_CHECK_MOVIE,
    // Puts dude [a] hexes from the nearest critter (out of combat).
    DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER,
    // Sets dude's action points to [a] (combat).
    DEV_AUTOTEST_ACTION_SET_ACTION_POINTS,
    // Adds [a] experience points to dude.
    DEV_AUTOTEST_ACTION_ADD_EXPERIENCE,
    // Logs dude's level, skill points, owed perks, perks, some skills.
    DEV_AUTOTEST_ACTION_LOG_CHARACTER,
    // Opens character creation (the step completes first, the editor loop
    // runs the script on).
    DEV_AUTOTEST_ACTION_OPEN_CHARACTER_CREATION,
    // Combat speed [a] (preferences), player speedup [b],
    // `combat_speed_all_animations` [c]; [a] < 0 - back to the settings the
    // test started with.
    DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED,
    // Starts dude's animation and logs how long it took when it ends: [a]
    // 0 - walks [b] hexes in direction [c], 1 - runs so, 2 - plays animation
    // type [b].
    DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION,
    // Every later frame takes [a] ms longer (a slow device).
    DEV_AUTOTEST_ACTION_SLOW_FRAMES,
    // Logs how long sound effect `widget` of dude's animation plays
    // (`animationLoadSoundEffect`: combat speed makes it shorter).
    DEV_AUTOTEST_ACTION_LOG_ANIMATION_SOUND,
    // Moves the map view by ([a], [b]) screen pixels every frame for [c]
    // frames (a finger dragging it).
    DEV_AUTOTEST_ACTION_PAN_FRAMES,
    // Logs frames, their average time and the world's uploads to the GPU
    // since the previous one (what the steps between cost).
    DEV_AUTOTEST_ACTION_PERF_LOG,
    // Dude steps (as walking does, the roof hides) onto the nearest tile
    // under a roof, [a] = 1 - back to where the previous one started.
    DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF,
    // A finger of its own (id 7): [a] 0 - down, 1 - up, 2 - down and up at
    // once (a system edge gesture re-sends touches so), at ([b], [c]) in
    // fractions of the window.
    DEV_AUTOTEST_ACTION_FINGER,
    // Taps mobile UI widget `widget` (down and up at once), then the game
    // stalls [a] ms before reading them.
    DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL,
    // Mobile UI widget `widget` is on screen ([a] 1) or not ([a] 0).
    DEV_AUTOTEST_ACTION_CHECK_WIDGET,
    // Leaves the map for map file `widget`, elevation [a], tile [b] (-1 -
    // its entrance), as an exit grid does (`mapSetTransition`); logs the
    // party's size.
    DEV_AUTOTEST_ACTION_MAP_TRANSITION,
    // Touch recorder (touch_log.h): [a] 0 - on, touch.log emptied; 1 - logs
    // what touch.log got, fails without a dump for each of `widget` and
    // `target` (reasons) and, [c] 1, without a "game closed" one; 2 - what
    // the game closing writes (`touchLogExit`).
    DEV_AUTOTEST_ACTION_TOUCH_LOG,
    // Main menu pictures (`muiPictureTexture`): the high resolution one by
    // path, the 640x480 one by id, a missing path, the cached one again.
    DEV_AUTOTEST_ACTION_CHECK_MENU_PICTURES,
    // Opens elevator [a]'s panel as a script does (the step completes first,
    // the panel's loop runs the script on), logs the level it returned.
    DEV_AUTOTEST_ACTION_OPEN_ELEVATOR,
    // Requests the ending slides as a script does (they start from the
    // game's loop).
    DEV_AUTOTEST_ACTION_REQUEST_ENDGAME,
    // Dude dies (the game shows its death screen, then the main menu).
    DEV_AUTOTEST_ACTION_KILL_DUDE,
    // World map: area [a] known and visited, the party stands at it.
    DEV_AUTOTEST_ACTION_WORLDMAP_AREA,
    // What scripts reading the mouse get (sfall `get_mouse_x/y`,
    // `tile_under_cursor`, `obj_under_cursor`, `get_mouse_buttons`) and how
    // many times `HOOK_MOUSECLICK` ran.
    DEV_AUTOTEST_ACTION_LOG_POINTER,
    // Mobile UI: drags widget `widget` by ([a], [b]) fractions of the window
    // (sliders).
    DEV_AUTOTEST_ACTION_MUI_DRAG_BY,
    // Logs the preferences and the settings the settings screen edits.
    DEV_AUTOTEST_ACTION_LOG_PREFERENCES,
    // Logs the saves (count, quick ones); FAIL when there aren't [a] of them.
    DEV_AUTOTEST_ACTION_LOG_SAVES,
};

struct DevAutotestStep {
    DevAutotestAction action;
    float a;
    float b;
    float c;
    // Frames to wait after the action before capturing.
    int delay;
    const char* name;
    // Mobile UI widget id.
    const char* widget;
    // Mobile UI widget to drag to.
    const char* target;
};

static void devAutotestLog(const char* format, ...);
static void devAutotestCheckDudeRoundTrip();

static const DevAutotestStep kDevAutotestSteps[] = {
    { DEV_AUTOTEST_ACTION_CHECK_MODS, 0, 0, 0, 1, "00_mods" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "01_zoom100" },
    { DEV_AUTOTEST_ACTION_ZOOM, 2.0f, -1, -1, 10, "02_zoom200" },
    { DEV_AUTOTEST_ACTION_ZOOM, 0.5f, -1, -1, 10, "03_zoom050" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.37f, 150, 100, 10, "04_zoom137_anchor" },
    { DEV_AUTOTEST_ACTION_PAN, 47.5f, 13.25f, 0, 10, "05_pan_small" },
    { DEV_AUTOTEST_ACTION_PAN, -420.0f, -260.0f, 0, 10, "06_pan_large" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 3.0f, 0, 0, 10, "07_zoom300_dude" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.0f, -1, -1, 10, "08_zoom100_again" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.2f, 0.4f, 0.0f, 10, "09_pinch_out" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.3f, 0.3f, 0.1f, 10, "10_two_finger_pan" },
    { DEV_AUTOTEST_ACTION_ZOOM, 2.0f, -1, -1, 10, "11_zoom200" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "11b_center_dude" },
    { DEV_AUTOTEST_ACTION_WALK, 4, 0, 0, 120, "12_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_WALK, 0, 0, 0, 1, "13_after_walk" },
    { DEV_AUTOTEST_ACTION_ZOOM, 3.0f, -1, -1, 10, "14_zoom300" },
    { DEV_AUTOTEST_ACTION_ARROW, 600, 150, 0, 30, "15_arrow_cursor_zoom300" },
    { DEV_AUTOTEST_ACTION_ZOOM, 2.5f, -1, -1, 10, "16_zoom250" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "17_center_dude" },
    { DEV_AUTOTEST_ACTION_TOUCH_DRAG, -150, -60, 0, 10, "18_touch_drag" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.5f, -1, -1, 10, "19_zoom150" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "19b_center_dude" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 4, ROTATION_SW, 0, 120, "20_touch_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_WALK, 0, 0, 0, 1, "21_after_touch_walk" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "21b_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_CRITTER, 0, 0, 0, 300, "21c_walk_to_critter" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "21d_center_dude" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "22_radial_menu" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_LOOK, 0, 0, 30, "23_radial_look" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "24a_give_knife" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "24b_give_stimpak" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 30, "24c_open_inventory" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "24d_inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "24e_inventory_tap", "inventory.pid.40" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "24f_inventory_long_press", "inventory.pid.4" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "24f2_inventory_radial_look", "radial.3" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 20, "24g_inventory_drag_to_hand", "inventory.pid.4", "inventory.slot.right" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "24h_inventory_after_drag" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "24i_close_inventory" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "24j_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 1, 0, 0, 20, "24k_aimed_hit_mode" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "24l_hit_mode_after" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "25_combat_started" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "26_combat_state" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 40, "27_combat_select_target" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "28_after_select" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "29_combat_aimed_attack" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "29b_called_shot", "calledshot.0" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "30_after_attack" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER, 0, 0, 0, 40, "31_combat_preview_move" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "32_after_preview_move" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER, 0, 0, 0, 90, "33_combat_move" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "34_after_move" },
    { DEV_AUTOTEST_ACTION_PAN_DURING_ENEMY_TURN, 150, 60, 20, 150, "34b_pan_enemy_turn" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "34c_after_enemy_turn" },
};

// World map of the mobile UI: "Enter" at a visited town (Klamath) opens its
// town map (Back returns), a finger moves the map, the towns' list opens
// over "Enter" and closes, the game menu (continue, settings, exit) opens
// and continues, a tap on the terrain sends the party there.
static const DevAutotestStep kDevAutotestWorldmapSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_map" },
    { DEV_AUTOTEST_ACTION_WORLDMAP_AREA, CITY_KLAMATH, 0, 0, 1, "w00b_at_klamath" },
    { DEV_AUTOTEST_ACTION_OPEN_WORLDMAP, 0, 0, 0, 1, "w01_open" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w02_worldmap" },
    { DEV_AUTOTEST_ACTION_LOG_WORLDMAP, 0, 0, 0, 1, "w03_state" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "w04_enter_shown", "worldmap.enter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "w05_town_map", "worldmap.enter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "w06_back_to_world", "townmap.back" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.25f, -0.15f, 0, 20, "w07_moved", "worldmap.map" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w08_towns", "worldmap.towns" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "w08b_enter_under_towns", "worldmap.enter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w09_towns_closed", "worldmap.towns.close" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w10_menu", "worldmap.menu" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "w10b_menu_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "w10c_menu_no_save", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "w11_menu_closed", "menu.continue" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "w11b_menu_gone", "menu.continue" },
    { DEV_AUTOTEST_ACTION_FINGER, 0, 0.75f, 0.7f, 2, "w12_tap_down" },
    { DEV_AUTOTEST_ACTION_FINGER, 1, 0.75f, 0.7f, 150, "w13_travelling" },
    { DEV_AUTOTEST_ACTION_LOG_WORLDMAP, 0, 0, 0, 1, "w14_state" },
};

#define HUD_ELEMENT(name) static_cast<float>(static_cast<int>(HudElementId::name))

static const DevAutotestStep kDevAutotestHudSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "h00_hud" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "h01_smg" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h02_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 2, 0, 0, 20, "h03_tap_mode_2" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h04_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 1, 0, 0, 20, "h05_tap_mode_1" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h06_hit_mode" },
    { DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE, 2, 0, 0, 20, "h06b_quick_tap_mode_2" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h06c_hit_mode_burst" },
    { DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE, 0, 0, 0, 20, "h06d_quick_tap_mode_0" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "h06e_hit_mode_single" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 7, HAND_LEFT, 0, 20, "h07_spear" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "h07b_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "h08_radial" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_LOOK, 0, 0, 20, "h09_toast" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Log), 0, 0, 20, "h10_log_open" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Log), 0, 0, 20, "h11_log_closed" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Sneak), 0, 0, 30, "h12_sneak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "h12b_give_stimpak" },
    // No party member yet: the skills list must stay usable (the HUD used
    // to close it every frame without the party orders button).
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Skills), 0, 0, 20, "h12c_skills_alone" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "h12d_sneak_from_list", "skills.0" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Highlight), 0, 0, 90, "h13_highlight" },
    { DEV_AUTOTEST_ACTION_HUD_LONG_PRESS, HUD_ELEMENT(Pipboy), 50, 0, 20, "h13a_pipboy_name" },
    { DEV_AUTOTEST_ACTION_FLOAT_TEXT, 0, 0, 0, 20, "h13f_float_text" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 0, 0, 30, "h13f2_sulik_joins" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(PartyOrders), 0, 0, 20, "h13g_party_orders" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h13h_order_auto_loot", "hud.orders.6" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Skills), 0, 0, 20, "h13j_skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h13k_first_aid_target", "skills.4" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 60, "h13l_first_aid_on_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "h13m_radial" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_USE_SKILL, 0, 0, 20, "h13n_skills_on_critter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "h13o_science_on_critter", "skills.6" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 0, 0, 1, "h13p_give_8" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 0, 0, 1, "h13p_give_29" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 0, 0, 1, "h13p_give_46" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 1, "h13p_give_74" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "h13p_give_4" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 10, 0, 0, 1, "h13p_give_10" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 47, 0, 0, 1, "h13p_give_47" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 48, 0, 0, 1, "h13p_give_48" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 49, 0, 0, 1, "h13p_give_49" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "h13p_radial" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL, GAME_MOUSE_ACTION_MENU_ITEM_INVENTORY, 0, 0, 20, "h13q_use_item_panel" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "h13q2_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "h13r_stimpak_on_critter", "useitem.pid.40" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "h13r2_log" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 11, 0, 0, 1, "h13b_slot_before" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickSave), 0, 0, 60, "h13c_quick_save" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 11, 0, 0, 1, "h13d_slot_after" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickSave), 0, 0, 60, "h13e_quick_save_2" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 12, 0, 0, 1, "h13f_slot_2" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Inventory), 0, 0, 30, "h14_inventory" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "h15_inventory_closed" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(SwapHands), 0, 0, 40, "h16_swap_hands" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "h17_combat" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "h18_combat_state" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(EndTurn), 0, 0, 120, "h19_end_turn" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "h20_after_end_turn" },
};

static const DevAutotestStep kDevAutotestMagnetSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "m00_map" },
    { DEV_AUTOTEST_ACTION_ZOOM, 0.5f, -1, -1, 10, "m01_zoom050" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m02_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 10, ROTATION_SW, 0, 8, "m03_walk" },
    { DEV_AUTOTEST_ACTION_LOG_MOVEMENT, 0, 0, 0, 1, "m04_walking" },
    // ~1.5 tiles aside at zoom 0.5 (radius is 24 dp = 32 px here).
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER, 14, 9, 0, 8, "m05_tap_near_marker" },
    { DEV_AUTOTEST_ACTION_LOG_MOVEMENT, 0, 0, 0, 150, "m06_running" },
    { DEV_AUTOTEST_ACTION_CHECK_WALK, 0, 0, 0, 1, "m07_arrived" },
    { DEV_AUTOTEST_ACTION_ZOOM, 3.0f, -1, -1, 10, "m08_zoom300" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m09_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 3, ROTATION_NE, 0, 8, "m10_walk" },
    // Neighbor tile at zoom 300: not snapped, walks there.
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER, 96, 0, 0, 8, "m11_tap_neighbor" },
    { DEV_AUTOTEST_ACTION_LOG_MOVEMENT, 0, 0, 0, 150, "m12_not_running" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.0f, -1, -1, 10, "m13_zoom100" },
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m14_center" },
    { DEV_AUTOTEST_ACTION_PLACE_ITEM, 40, 4, ROTATION_SE, 10, "m15_item" },
    // Without highlight a tap next to an item walks.
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM, 12, 12, 0, 200, "m16_tap_near_item" },
    { DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM, 0, 0, 0, 1, "m17_not_picked" },
    // With highlight: tap on the middle of a spear's box (thin sprite).
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m18_center" },
    { DEV_AUTOTEST_ACTION_PLACE_ITEM, 7, 4, ROTATION_NE, 10, "m19_spear" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Highlight), 0, 0, 20, "m20_highlight" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM, 0, 0, 1, 200, "m21_tap_spear" },
    { DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM, 0, 0, 0, 1, "m22_spear_picked" },
    // With highlight: 25 px off (radius 24 dp = 32 px).
    { DEV_AUTOTEST_ACTION_CENTER_DUDE, 0, 0, 0, 10, "m23_center" },
    { DEV_AUTOTEST_ACTION_PLACE_ITEM, 40, 4, ROTATION_SE, 10, "m24_item" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM, 18, 18, 0, 12, "m25_tap_near_item_highlight" },
    // Dude walks to the item: it's outlined green (map hints).
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 190, "m25b_walked" },
    { DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM, 0, 0, 0, 1, "m26_picked" },
};

static const DevAutotestStep kDevAutotestUiSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "u00_map" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickLoad), 0, 0, 30, "u01_quick_load_dialog" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "u02_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickLoad), 0, 0, 30, "u03_dialog_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 1, 0, 0, 20, "u04_quick_tap_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 10, "u05_closed" },
};

static const DevAutotestStep kDevAutotestTalkSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "t00_map" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "t01_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "t02_talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "t03_option_0", "talk.options.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "t04_review", "dialog.tab.review" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "t05_review_closed" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 1, 0, 0, 90, "t06_option_0_quick", "talk.options.0" },
};

static const DevAutotestStep kDevAutotestBarterSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "b01_give_stimpak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "b02_give_stimpak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "b03_give_stimpak" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "b04_give_knife" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "b05_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "b06_talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "b07_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b08_tap_item", "barter.player.pid.40" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b09_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "b10_drag_item", "barter.player.pid.40", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "b10b_quantity_plus", "quantity.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b10c_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b11_filter_weapons", "barter.filter.player.1" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "b11b_item_menu", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "b11c_look", "radial.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b12_offer", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "b13_back_to_talk", "dialog.tab.talk" },
};

// A conversation through several branches with barter in the middle; the
// logs of the dialog's state are compared between builds.
static const DevAutotestStep kDevAutotestDialogTraceSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "d00_map" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d00b_windows" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d00c_state" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 2, 0, 1, "d01_give_stimpaks" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 41, 150, 0, 1, "d01b_give_money" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "d02_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "d03_talk" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d03b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d03c_windows" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "d04_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d04b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d04c_windows" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d05_request_item", "barter.barterer.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d05b_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d06_offer_too_little", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d06b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07_offer_item", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07b_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07c_offer_item", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d07d_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d07e_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d08_offer", "barter.offer.button" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d08b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d09_left_on_table", "barter.player.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d09b_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d09c_state" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 90, "d10_back_to_talk" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d10b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d10c_windows" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d11_ask", "talk.options.1" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d11b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d12_fine", "talk.options.0" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d12b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d12c_windows" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "d13_talk_again" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d13b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d14_trial", "talk.options.2" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d14b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "d15_option_0", "talk.options.0" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "d15b_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "d15c_windows" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "d16_inventory" },
};

// A talk with a talking head (the Elder of Arroyo, `--dev-map=arvillag.map`),
// by the mobile UI's way or the game's windows; the logs are compared.
static const DevAutotestStep kDevAutotestHeadTraceSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "h00_map" },
    { DEV_AUTOTEST_ACTION_LOG_CRITTERS, 0, 0, 0, 1, "h00b_critters" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "h00d_windows" },
    { DEV_AUTOTEST_ACTION_TALK_TO_PID, 13, 0, 0, 30, "h01_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 900, 0, 0, 60, "h01b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h01c_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "h01d_windows" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h02_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h02b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h02c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h03_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h03b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h03c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 1, 0, 0, 30, "h04_choose_1" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h04b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h04c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h05_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h05b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h05c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h06_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h06b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h06c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h07_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h07b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h07c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h08_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h08b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h08c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 30, "h09_choose_0" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 60, "h09b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "h09c_state" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "h99_windows" },
};

// Barter with Clint (at the new game's start) by the mobile UI's way or the
// game's windows; the logs are compared.
static const DevAutotestStep kDevAutotestBarterTraceSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "r00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 41, 20, 0, 1, "r01_give_money" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "r02_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "r03_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 900, 0, 0, 30, "r03b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r03c_state" },
    { DEV_AUTOTEST_ACTION_DIALOG_BARTER, 0, 0, 0, 90, "r04_barter" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r04b_state" },
    { DEV_AUTOTEST_ACTION_BARTER_OFFER, 0, 0, 0, 30, "r05_offer_nothing" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r05b_state" },
    { DEV_AUTOTEST_ACTION_BARTER_TALK, 0, 0, 0, 30, "r06_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 30, "r06b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r06c_state" },
    { DEV_AUTOTEST_ACTION_DIALOG_BARTER, 0, 0, 0, 90, "r07_barter_again" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r07b_state" },
    { DEV_AUTOTEST_ACTION_BARTER_TALK, 0, 0, 0, 30, "r08_talk" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 30, "r08b_talking" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 1, 0, 0, 30, "r09_ask" },
    { DEV_AUTOTEST_ACTION_WAIT_TALKING, 600, 0, 0, 30, "r09b_talking" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r09c_state" },
    { DEV_AUTOTEST_ACTION_CHOOSE_OPTION, 0, 0, 0, 90, "r10_fine" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "r10b_state" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "r11_inventory" },
};

// The game's interface bar (run with [touch] hud=0 and mobile_ui=0): hands,
// attack modes, combat's end buttons, by the game's keys.
static const DevAutotestStep kDevAutotestPcBarSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "a01_smg" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_N, 0, 0, 20, "a02_next_mode" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_B, 0, 0, 60, "a03_swap_hands" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_B, 0, 0, 60, "a04_swap_back" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 120, "a05_combat" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_SPACE, 0, 0, 240, "a06_end_turn" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_RETURN, 0, 0, 240, "a07_end_combat" },
};

static Object* gDevAutotestPlacedItem = nullptr;

// Trace scenarios: the items' state is logged after every step.
static bool gDevAutotestTraceItems = false;

// Windows there were at the first LOG_NEW_WINDOWS.
static std::set<int> gDevAutotestStartWindows;
static bool gDevAutotestStartWindowsTaken = false;
static Object* gDevAutotestCorpse = nullptr;
static Object* gDevAutotestPartyMember = nullptr;

static const DevAutotestStep* gDevAutotestSteps = kDevAutotestSteps;
static int gDevAutotestStepCount = sizeof(kDevAutotestSteps) / sizeof(kDevAutotestSteps[0]);

// Synthetic single finger touch: finger goes down at start, stays there for
// `holdFrames`, moves to end during `moveFrames` and goes up.
struct DevAutotestTouch {
    float startX;
    float startY;
    float endX;
    float endY;
    int holdFrames;
    int moveFrames;
    // Frame to capture the screen at while touching (-1 - none).
    int captureFrame;
    const char* captureName;
};

static DevAutotestTouch gDevAutotestTouch;
static int gDevAutotestTouchFrame = -1;
// Frames to wait before the touch starts.
static int gDevAutotestTouchDelay = 0;

static int gDevAutotestWalkTarget = -1;
static int gDevAutotestTouchStartTile = -1;

static constexpr int kDevAutotestPinchFrames = 20;

static bool gDevAutotestEnabled = false;
static std::string gDevAutotestOutputPath;
static int gDevAutotestStep = 0;
static int gDevAutotestDelay = 0;
static bool gDevAutotestStepStarted = false;

// TIME_DUDE_ANIMATION being timed.
static const char* gDevAutotestTimedName = nullptr;
static unsigned int gDevAutotestTimedStart = 0;
static int gDevAutotestTimedTile = -1;

static int gDevAutotestSlowFrameMs = 0;

// WALK_UNDER_ROOF: where dude went in from.
static int gDevAutotestRoofStartTile = -1;

// PAN_FRAMES in progress.
static float gDevAutotestPanX = 0.0f;
static float gDevAutotestPanY = 0.0f;
static int gDevAutotestPanFrames = 0;

// PERF_LOG: frames so far, the previous log's frame, time and uploads.
static int gDevAutotestFrames = 0;
static int gDevAutotestPerfFrame = 0;
static unsigned int gDevAutotestPerfTime = 0;
static long long gDevAutotestPerfUploads = 0;
static long long gDevAutotestPerfPixels = 0;
static long long gDevAutotestPerfDrawn = 0;
static const char* gDevAutotestPendingCapture = nullptr;
static int gDevAutotestPinchFrame = -1;

static void devAutotestPushFinger(Uint32 type, SDL_FingerID finger, float x, float y)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.tfinger.touchId = 1;
    event.tfinger.fingerId = finger;
    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.pressure = 1.0f;
    SDL_PushEvent(&event);
}

// Moves two synthetic fingers symmetrically around a centroid. Returns true
// when the gesture is complete.
static bool devAutotestPinchTick(const DevAutotestStep* step)
{
    gDevAutotestPinchFrame++;

    float progress = static_cast<float>(gDevAutotestPinchFrame) / kDevAutotestPinchFrames;
    float span = step->a + (step->b - step->a) * std::min(progress, 1.0f);
    float centerX = 0.5f + step->c * std::min(progress, 1.0f);
    float centerY = 0.4f;

    Uint32 type = gDevAutotestPinchFrame == 0 ? SDL_FINGERDOWN : SDL_FINGERMOTION;
    if (gDevAutotestPinchFrame > kDevAutotestPinchFrames) {
        type = SDL_FINGERUP;
    }

    devAutotestPushFinger(type, 1, centerX - span / 2, centerY);
    devAutotestPushFinger(type, 2, centerX + span / 2, centerY);

    return type == SDL_FINGERUP;
}

static void devAutotestStartTouch(float startX, float startY, float endX, float endY, int holdFrames, int moveFrames, int captureFrame = -1, const char* captureName = nullptr)
{
    gDevAutotestTouch = { startX, startY, endX, endY, holdFrames, moveFrames, captureFrame, captureName };
    gDevAutotestTouchFrame = 0;
    devAutotestLog("  touch (%.0f, %.0f) -> (%.0f, %.0f), hold %d, move %d frames, world %d, map input %d\n", startX, startY, endX, endY, holdFrames, moveFrames,
        worldViewIsWorldAt(static_cast<int>(startX), static_cast<int>(startY)) ? 1 : 0,
        gameMouseIsMapInputEnabled() ? 1 : 0);
}

// Returns true when the touch is complete.
static bool devAutotestTouchTick()
{
    if (gDevAutotestTouchDelay > 0) {
        gDevAutotestTouchDelay--;
        return false;
    }

    const DevAutotestTouch& touch = gDevAutotestTouch;
    int frame = gDevAutotestTouchFrame++;

    if (frame == touch.captureFrame) {
        gDevAutotestPendingCapture = touch.captureName;
    }

    float width = static_cast<float>(screenGetWidth());
    float height = static_cast<float>(screenGetHeight());

    if (frame == 0) {
        devAutotestPushFinger(SDL_FINGERDOWN, 1, touch.startX / width, touch.startY / height);

        // Quick tap: finger up arrives with finger down.
        if (touch.holdFrames < 0) {
            devAutotestPushFinger(SDL_FINGERUP, 1, touch.endX / width, touch.endY / height);
            gDevAutotestTouchFrame = -1;
            return true;
        }
        return false;
    }

    if (frame <= touch.holdFrames) {
        return false;
    }

    if (frame <= touch.holdFrames + touch.moveFrames) {
        float progress = static_cast<float>(frame - touch.holdFrames) / touch.moveFrames;
        float x = touch.startX + (touch.endX - touch.startX) * progress;
        float y = touch.startY + (touch.endY - touch.startY) * progress;
        devAutotestPushFinger(SDL_FINGERMOTION, 1, x / width, y / height);
        return false;
    }

    devAutotestPushFinger(SDL_FINGERUP, 1, touch.endX / width, touch.endY / height);
    gDevAutotestTouchFrame = -1;
    return true;
}

// Returns the nearest visible critter other than dude.
// Script [fileName] (scripts.lst) on [critter], as create_object_sid does.
static void devAutotestAttachScript(Object* critter, const char* fileName)
{
    if (fileName == nullptr) {
        return;
    }

    int index = -1;
    char name[64];
    for (int candidate = 0; candidate < scriptsGetListLength(); candidate++) {
        if (scriptsGetFileName(candidate, name, sizeof(name)) == 0 && compat_stricmp(name, fileName) == 0) {
            index = candidate;
            break;
        }
    }

    if (index == -1) {
        devAutotestLog("  script %s: NOT FOUND\n", fileName);
        return;
    }

    if (critter->sid != -1) {
        scriptRemove(critter->sid);
        critter->sid = -1;
    }

    Script* script;
    if (scriptAdd(&(critter->sid), SCRIPT_TYPE_CRITTER) == -1 || scriptGetScript(critter->sid, &script) == -1) {
        return;
    }

    script->index = index;
    critter->id = scriptsNewObjectId();
    script->ownerId = critter->id;
    script->owner = critter;
    _scr_find_str_run_info(index, &(script->field_50), critter->sid);
    devAutotestLog("  script %s (index %d)\n", fileName, index);
}

static Object* devAutotestFindNearestCritter()
{
    Object* critter = nullptr;
    int bestDistance = 0;
    for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
        if (object == gDude || FrmId(object).objectType() != OBJ_TYPE_CRITTER || (object->flags & OBJECT_HIDDEN) != 0 || critterIsDead(object)) {
            continue;
        }

        int distance = objectGetDistanceBetween(gDude, object);
        if (critter == nullptr || distance < bestDistance) {
            critter = object;
            bestDistance = distance;
        }
    }
    return critter;
}

// Returns screen position of the center of the object.
static void devAutotestGetObjectScreenPosition(Object* object, float* x, float* y)
{
    Rect rect;
    objectGetRect(object, &rect);
    int screenX;
    int screenY;
    worldViewWorldToScreen((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2, &screenX, &screenY);
    *x = static_cast<float>(screenX);
    *y = static_cast<float>(screenY);
}

// Returns screen position of the center of the tile.
static void devAutotestGetTileScreenPosition(int tile, float* x, float* y)
{
    int worldX;
    int worldY;
    tileToScreenXY(tile, &worldX, &worldY);
    int screenX;
    int screenY;
    worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
    *x = static_cast<float>(screenX);
    *y = static_cast<float>(screenY);
}

// Logs whether files only present in Restoration Project are visible.
static void devAutotestCheckMods()
{
    const char* paths[] = {
        "maps\\abbey.map",
        "art\\intrface\\twnabbey.frm",
    };

    for (const char* path : paths) {
        int size = 0;
        bool found = dbGetFileSize(path, &size) == 0;
        devAutotestLog("  mod file %s: %s (%d bytes)\n", path, found ? "found" : "MISSING", size);
    }
}

// Mobile UI inventory screen: ammo into a weapon, item into a bag, armor
// and hands, bag opened and closed, item taken out, dropping.
static const DevAutotestStep kDevAutotestInventorySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "i00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 0, 0, 1, "i01_give_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 0, 0, 1, "i02_give_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 0, 0, 1, "i02b_give_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 0, 0, 1, "i03_give_bag" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 1, "i04_give_jacket" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "i05_give_knife" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 30, "i06_open" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i06b_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i06c_turn", "inventory.figure", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i07_info_pistol", "inventory.pid.8" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "i07b_pistol_menu", "inventory.pid.8" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i07c_unload", "radial.7" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i07d_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i08_ammo_into_pistol", "inventory.pid.29", "inventory.pid.8" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i08b_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i09_knife_into_bag", "inventory.pid.4", "inventory.pid.46" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i10_wear_jacket", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i11_pistol_to_hand", "inventory.pid.8", "inventory.slot.right" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i11b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i12_info_jacket", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "i13_bag_menu", "inventory.pid.46" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 20, 0, 0, 30, "i14_open_bag", "radial.6" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i15_take_knife_out", "inventory.list.0", "inventory.container" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i16_close_bag", "inventory.container" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i16b_log" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i17_drop_knife", "inventory.pid.4", "inventory.info" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i18_filter_ammo", "inventory.filter.5" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "i19_drop_all", "inventory.drop_all" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i20_drop_all_confirm", "inventory.drop_all" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "i21_filter_all", "inventory.filter.0" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21b_log" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "i21c_give_stimpak" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "i21d_stimpak_menu", "inventory.pid.40" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 3, "i21e_use_stimpak", "radial.6" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 10, "i21f_after_use" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "i21g_after_use" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 10, "i21h_give_drag_stimpak" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21i_before_drag_use" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i21j_drag_stimpak_to_figure", "inventory.pid.40", "inventory.figure" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21k_after_drag_use" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 10, "i21l_give_nonconsumable" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "i21m_drag_knife_to_figure", "inventory.pid.4", "inventory.figure" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "i21n_after_rejected_drag" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "i22_back", "inventory.back" },
};

// The game's inventory and loot windows (run with [touch] mobile_ui=0,
// out/dev/runpc.sh): they open, show and close by Esc.
static const DevAutotestStep kDevAutotestPcInventorySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_map" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w00b_windows" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 0, 0, 1, "w01_give_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 0, 0, 1, "w02_give_bag" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 30, "w03_inventory" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w03b_windows" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 30, "w04_closed" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w04b_windows" },
    { DEV_AUTOTEST_ACTION_KILL_CRITTER, 0, 0, 0, 60, "w05_kill" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 29, 0, 0, 1, "w06_corpse_ammo" },
    { DEV_AUTOTEST_ACTION_CENTER_CORPSE, 0, 0, 0, 10, "w07_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE, 0, 0, 0, 240, "w08_loot" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w08b_windows" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_A, 0, 0, 30, "w09_take_all" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 30, "w10_closed" },
    { DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS, 0, 0, 0, 1, "w10b_windows" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "w11_inventory" },
};

// Mobile UI loot screen: moving items both ways, filtered "Take all", a
// bag opened on the looted side, "Give all".
static const DevAutotestStep kDevAutotestLootSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "l00_map" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 0, 0, 1, "l01_give_knife" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 0, 0, 1, "l02_give_stimpak" },
    { DEV_AUTOTEST_ACTION_KILL_CRITTER, 0, 0, 0, 60, "l03_kill" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 8, 0, 0, 1, "l04_corpse_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 29, 0, 0, 1, "l05_corpse_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 46, 0, 0, 1, "l06_corpse_bag" },
    { DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM, 40, 0, 0, 1, "l07_corpse_stimpak" },
    { DEV_AUTOTEST_ACTION_CENTER_CORPSE, 0, 0, 0, 10, "l08_center" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE, 0, 0, 0, 240, "l09_open_loot" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "l09b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l10_take_pistol", "loot.right.pid.8" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l11_give_knife", "loot.left.pid.4", "loot.right" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "l12_filter_ammo", "loot.filter.right.5" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l13_take_ammo", "loot.take_all" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "l14_filter_all", "loot.filter.right.0" },
    { DEV_AUTOTEST_ACTION_MUI_LONG_PRESS, 45, 0, 0, 20, "l15_bag_menu", "loot.right.pid.46" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l16_open_bag", "radial.6" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l17_close_bag", "loot.container.right" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l18_take_all", "loot.take_all" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "l18b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "l19_filter_drugs", "loot.filter.left.4" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l20_give_all", "loot.give_all" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "l20b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "l21_back", "loot.back" },
};

// Test save for the phone: Arroyo village, varied inventory, two party
// members (run with --dev-map=arvillag.map).
static const DevAutotestStep kDevAutotestLoadSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "v01_loaded" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "v02_log" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "v03_party" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(PartyOrders), 0, 0, 20, "v04_orders" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 12, "v05_holster", "hud.orders.0" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "v06_party_after" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 200, "v07_talk_nearest" },
};

// Own empty SAVEGAME, test game's quick saves: one page from page 1. Check actual engine
// round trips as well as making a quick save permanent, new saves and session
// quickload.
static const DevAutotestStep kDevAutotestSaveHistorySteps[] = {
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h00_name", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SAVE_GAME, 0, 0, 0, 10, "h01_manual", "Manual anchor" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 0, 0, 0, 1, "h02_anchor", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h03_name", "QUICK" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 10, "h04_quick" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 10, 0, 0, 1, "h05_quick_anchor", "QUICK" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 0, 0, 0, 20, "h06_old_manual" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 0, 0, 0, 1, "h07_loaded_anchor", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h08_dirty", "DIRTY" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 20, "h09_quick_load_manual" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 0, 0, 0, 1, "h10_restored", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 10, "h11_next_quick" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 11, 0, 0, 1, "h12_independent_ring", "ANCHOR" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h13_name", "MANUAL" },
    { DEV_AUTOTEST_ACTION_SAVE_GAME, 1, 0, 0, 10, "h14_new_manual", "Latest manual" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "h15_dirty", "DIRTY" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 20, "h16_quick_load_new_manual" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 1, 0, 0, 1, "h17_restored", "MANUAL" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 20, "h18_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h19_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h20_select_quick", "loadsave.slots.11" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h21_permanent", "loadsave.permanent" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 1, 0, 0, 1, "h22_permanent_keeps_anchor", "MANUAL" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "h23_load_permanent", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 2, 0, 0, 1, "h24_permanent_is_original_snapshot", "QUICK" },
    { DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD, 0, 0, 0, 20, "h25_world" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 20, "h26_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h27_save", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h28_save_selected", "loadsave.slots.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "h29_new_not_overwrite", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "h30_save", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION, 3, 0, 0, 1, "h31_new_manual_anchor", "QUICK" },
};

// Native menu and save/load flow: create a save, select an existing one, return to New, load the original.
static const DevAutotestStep kDevAutotestMuiLoadSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "m00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "m01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "m02_save", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "m03_description", "loadsave.description" },
    { DEV_AUTOTEST_ACTION_TYPE_TEXT, 0, 0, 0, 10, "m04_type", " TEST" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m05_done", "loadsave.description.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 100, "m06_save_game", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 1, 0, 0, 1, "m07_saved" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "m08_menu_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "m09_save_again", "menu.save" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m10_existing", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m11_new_save", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "m13_back", "loadsave.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "m14_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "m15_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 100, "m16_load_game", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "m17_loaded" },
};

// Settings screen from the game menu: a change marks its row and section,
// volumes preview live, Back with changes asks (No keeps the settings), Apply
// sets them, Reset all (confirmed) and Yes on Back set the defaults.
// runtest.sh puts fallout2.cfg back after the test.
// The settings' "Game files" section: its actions ask first (Android does
// them in the app's import screen).
static const DevAutotestStep kDevAutotestGameFilesSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "g00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "g01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g02_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g03_files", "prefs.sections.4" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g04_export_asks", "prefs.files.export" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g05_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g06_delete_asks", "prefs.files.delete" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g07_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "g08_game", "prefs.sections.0" },
};

static const DevAutotestStep kDevAutotestPreferencesSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p01_before" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "p02_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p03_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p04_violence", "prefs.rows.251.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p05_combat", "prefs.sections.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.1f, 0, 0, 40, "p06_speed_dragged", "prefs.rows.265" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p07_all_animations", "prefs.rows.267" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p08_sound", "prefs.sections.2" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.1f, 0, 0, 40, "p09_master_dragged", "prefs.rows.273" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p10_display", "prefs.sections.3" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.5f, 0, 0, 40, "p11_brightness_dragged", "prefs.rows.277" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p12_back_asks", "prefs.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "p13_no", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "p14_closed", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p15_unchanged" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p16_settings_again", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p17_combat", "prefs.sections.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.1f, 0, 0, 40, "p18_speed_dragged", "prefs.rows.265" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p19_all_animations", "prefs.rows.267" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "p20_applied", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p21_applied" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p22_reset_asks", "prefs.reset" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p23_reset", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p24_back_asks", "prefs.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "p25_yes", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "p26_closed", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_PREFERENCES, 0, 0, 0, 1, "p27_defaults" },
};

// Own SAVEGAME, 16 saves (manual 1, quick 11-20, manual 21-25): the number of
// quick saves grows (no question), goes off (asked: they become permanent)
// and back on - no save lost on the way.
static const DevAutotestStep kDevAutotestQuickSavesSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "q00_start" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q01_saves" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "q02_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "q03_settings", "menu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0, -0.45f, 0, 40, "q04_scrolled", "prefs.rows.253" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.15f, 0, 0, 40, "q05_more", "prefs.rows.360" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q06_applied", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q07_saves" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.6f, 0, 0, 40, "q08_off", "prefs.rows.360" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q09_asks", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q10_yes", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q11_saves" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, 0.03f, 0, 0, 40, "q12_on", "prefs.rows.360" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "q13_applied", "prefs.apply" },
    { DEV_AUTOTEST_ACTION_LOG_SAVES, 16, 0, 0, 1, "q14_saves" },
};

// Own SAVEGAME with a copy of the test save in slot 1: cancel once, then delete it.
static const DevAutotestStep kDevAutotestMuiDeleteSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "d00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "d01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d02_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d03_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d04_delete", "loadsave.delete" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d05_cancel", "dialog.secondary" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 1, 0, 0, 1, "d06_still_saved" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d07_delete_again", "loadsave.delete" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d08_confirm", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_LOG_SLOT, 1, 0, 0, 1, "d09_deleted" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "d10_back", "loadsave.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "d11_save", "menu.save" },
};

// Own SAVEGAME with a broken copy of the test save: loading it shows the
// game's error and goes back to the main menu, as the game's window does.
static const DevAutotestStep kDevAutotestMuiLoadFailedSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b00_start" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "b01_menu" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b02_load", "menu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "b03_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "b04_load_game", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "b05_error", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b06_main_menu" },
};

// Run from the title screen without --dev-new-game. The Load button opens
// the real main-menu load path and exercises its palette/preview state.
static const DevAutotestStep kDevAutotestMuiMainMenuLoadSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "t00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 50, "t01_load", "mainmenu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "t02_slot", "loadsave.slots.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "t03_back", "loadsave.back" },
};

// Go through the actual title-screen Load action and capture the world after
// the picker closes. This catches a palette left black by the menu fade.
static const DevAutotestStep kDevAutotestMuiMainMenuResumeSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "r00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 50, "r01_picker", "mainmenu.load" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "r02_newest_selected" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "r03_load", "loadsave.action" },
    { DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD, 0, 0, 0, 90, "r04_world" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 4, ROTATION_SW, 0, 120, "r05_touch_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED, 0, 0, 0, 1, "r06_moved" },
};

static const DevAutotestStep kDevAutotestMuiMainMenuEmptySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "e00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 50, "e01_load_empty", "mainmenu.load" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "e02_back", "loadsave.back" },
};

// The game's own party member control screens (run with [touch]
// mobile_ui=0): talk to Sulik from the test save, combat control, custom.
static const DevAutotestStep kDevAutotestPcPartySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "p01_loaded" },
    { DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER, 0, 0, 0, 150, "p02_talk" },
    { DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW, 600, 123, 0, 90, "p03_party_control" },
    { DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW, 446, 164, 0, 90, "p04_custom" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 60, "p05_back_to_control" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_ESCAPE, 0, 0, 60, "p06_back_to_talk" },
    { DEV_AUTOTEST_ACTION_LOG_DIALOG, 0, 0, 0, 1, "p07_state" },
};

// The game's own character screen (run with [touch] mobile_ui=0).
static const DevAutotestStep kDevAutotestPcCharacterSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "c00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "c01_loaded" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_C, 0, 0, 60, "c02_character" },
};

// Mobile character screen: level up with an owed perk (test save).
static const DevAutotestStep kDevAutotestCharacterSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "k00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "k01_loaded" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k01b_log" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 40, "k01c_no_points" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k01d_no_points_skills", "character.tab.skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k01e_closed", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_ADD_EXPERIENCE, 7000, 0, 0, 60, "k02_xp" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k02b_log" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 60, "k03_open" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k04_pick", "character.perkpanel.list.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "k05_taken", "character.perkpanel.take" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k05b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k06_skills", "character.tab.skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "k07_plus", "character.skills.0.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k07b_plus", "character.skills.0.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k07c_minus", "character.skills.1.minus" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k07d_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k08_unarmed", "character.skills.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k09_perks", "character.tab.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k09b_perk", "character.folder.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k10_karma", "character.folder.karma" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k11_kills", "character.folder.kills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k12_stats", "character.tab.stats" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k13_carry", "character.derived.2" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k14_hp", "character.hp" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k15_reset", "character.reset" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k15b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k16_perks_again", "character.tab.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "k16b_perks_folder", "character.folder.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k17_owed", "character.perks.owed" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "k18_later", "character.perkpanel.later" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "k19_inventory", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "k19b_to_character", "inventory.character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "k19c_to_inventory", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "k20_closed" },
    { DEV_AUTOTEST_ACTION_LOG_CHARACTER, 0, 0, 0, 1, "k20b_log" },
};

// Before the vault suit (a new game) there's no Pip-Boy, as in combat: the
// HUD's button shows the game's box, the screens' tab keeps the screen (a
// notice says why).
static const DevAutotestStep kDevAutotestPipboyNotWornSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "q00_map" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Pipboy), 0, 0, 20, "q01_hud_pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "q02_box_closed", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 180, "q03_character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "q04_tab_pipboy", "character.nav.pipboy" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "q05_still_character", "character.nav.pipboy" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "q06_no_box", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "q07_closed" },
};

// Game screen tabs in combat: inventory cost badge, not enough action points
// and no Pip-Boy keep the screen.
static const DevAutotestStep kDevAutotestCombatScreensSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 45, "x00_map" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "x01_combat" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "x01b_state" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 2, 0, 0, 5, "x01c_ap_2" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 5, "x01d_hud_unavailable" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Pipboy), 0, 0, 20, "x01e_hud_pipboy_blocked" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "x01e_dialog_closed", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(Inventory), 0, 0, 20, "x01f_hud_inventory_blocked" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Character), 0, 0, 180, "x02_character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x03_pipboy_blocked", "character.nav.pipboy" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 2, 0, 0, 180, "x04_ap_2" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x05_inventory_blocked", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 180, "x05b_map", "character.nav.map" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 5, "x05c_map_unavailable" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x05d_map_inventory_blocked", "automap.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "x05e_character", "automap.character" },
    { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 10, 0, 0, 5, "x06_ap_10" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "x07_inventory", "character.nav.inventory" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "x07b_state" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "x08_character_again", "inventory.character" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "x09_map", "character.nav.back" },
};

// The game's Pip-Boy window (PC mode): its sections.
static const DevAutotestStep kDevAutotestPcPipboySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "p01_loaded" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_P, 0, 0, 60, "p02_pipboy" },
    { DEV_AUTOTEST_ACTION_EVENT, 500, 0, 0, 30, "p03_status" },
    { DEV_AUTOTEST_ACTION_EVENT, 506, 0, 0, 30, "p04_location" },
    { DEV_AUTOTEST_ACTION_EVENT, 500, 0, 0, 20, "p05_status_again" },
    { DEV_AUTOTEST_ACTION_EVENT, 501, 0, 0, 30, "p06_automaps" },
    { DEV_AUTOTEST_ACTION_EVENT, 506, 0, 0, 30, "p07_automap_location" },
    { DEV_AUTOTEST_ACTION_EVENT, 502, 0, 0, 30, "p08_archives" },
    { DEV_AUTOTEST_ACTION_EVENT, 503, 0, 0, 30, "p09_alarm" },
};

// Mobile Pip-Boy (test save with some quests and holodisks set).
static const DevAutotestStep kDevAutotestPipboySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "y00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "y01_loaded" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 183, 3, 0, 1, "y01b_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 191, 1, 0, 1, "y01c_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 100, 1, 0, 1, "y01d_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 101, 2, 0, 1, "y01e_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 550, 1, 0, 1, "y01f_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 221, 1, 0, 1, "y01g_holodisk" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 222, 1, 0, 1, "y01h_holodisk" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Pipboy), 0, 0, 60, "y02_pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y03_den", "pipboy.quests.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y04_data", "pipboy.section.data" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y05_data_2", "pipboy.data.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y06_maps", "pipboy.section.maps" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.15f, 0.4f, 0.0f, 20, "y06b_pinch_in" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.2f, 0.2f, -0.05f, 20, "y06c_two_finger_pan" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y07_videos", "pipboy.section.videos" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "y07b_playing", "pipboy.videos.play" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y07c_paused", "pipboy.videos.pause" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 40, "y07d_still_paused" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "y07e_resumed", "pipboy.videos.pause" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "y07f_fullscreen", "pipboy.videos.fullscreen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "y07g_collapsed", "pipboy.videos.fullscreen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "y07h_stopped", "pipboy.videos.stop" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y08_rest", "pipboy.section.rest" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "y09_rest_hour", "pipboy.rest.2" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "y10_rested" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "y11_to_inventory", "pipboy.inventory" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "y12_to_pipboy", "inventory.pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "y13_closed", "pipboy.back" },
};

// Mobile map screen (automap of the current map).
static const DevAutotestStep kDevAutotestAutomapSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "a01_loaded" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Automap), 0, 0, 60, "a02_map" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.15f, 0.4f, 0.0f, 20, "a03_pinch_in" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.2f, 0.2f, 0.08f, 20, "a04_two_finger_pan" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a05_details", "automap.details" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "a06_sensor", "automap.sensor" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "a07_to_pipboy", "automap.pipboy" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "a08_to_map", "pipboy.map" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "a09_closed", "automap.back" },
};

// Game movies through the mobile UI player; a tap skips.
static const DevAutotestStep kDevAutotestMovieSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_start" },
    { DEV_AUTOTEST_ACTION_PLAY_MOVIE, 3, 0, 0, 1, "v01_start_movie" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "v02_playing" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v03_playing_later" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "v04_skipped", "movie.screen" },
};

// Real title-menu paths, including the premade-character selector.
static const DevAutotestStep kDevAutotestTitleIntroSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "vi00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "vi01_intro_tap", "mainmenu.intro" },
    { DEV_AUTOTEST_ACTION_CHECK_MOVIE, 0, 0, 0, 1, "vi02_playing" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "vi03_frame" },
};

static const DevAutotestStep kDevAutotestNewGameElderSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "ve00_title" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "ve01_new_game_tap", "mainmenu.new" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "ve02_take_character", "selector.play" },
    { DEV_AUTOTEST_ACTION_CHECK_MOVIE, 0, 0, 0, 1, "ve03_playing" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "ve04_frame" },
};

// Title screen of the mobile UI: settings and back, the character selector
// (tabs, a swipe, the own character's card, back), Continue loads the save
// made last.
static const DevAutotestStep kDevAutotestMainMenuSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 40, "mm00_title" },
    { DEV_AUTOTEST_ACTION_CHECK_MENU_PICTURES, 0, 0, 0, 1, "mm00b_picture_resources" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm01_continue_shown", "mainmenu.continue" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm02_settings", "mainmenu.settings" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "mm03_back_to_title", "prefs.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "mm04_selector", "mainmenu.new" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "mm05_second", "selector.tabs.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG_BY, -0.2f, 0, 0, 40, "mm06_swiped_to_third", "selector.picture" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "mm07_own", "selector.tabs.3" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm08_create_shown", "selector.create" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm09_creating", "selector.create" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm10_editor_shown", "character.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm11_cancelled", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm12_own_again", "selector.create" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "mm13_first", "selector.tabs.0" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm14_modifying", "selector.modify" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "mm15_cancelled", "character.nav.back" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "mm16_premade_again", "selector.play" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "mm17_back_to_title", "selector.back" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "mm18_continue", "mainmenu.continue" },
    { DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD, 0, 0, 0, 30, "mm19_world" },
};

// Elevator panel of the mobile UI: Back stays, then a level's button moves
// the gauge and returns that level.
static const DevAutotestStep kDevAutotestElevatorSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "el00_start" },
    { DEV_AUTOTEST_ACTION_OPEN_ELEVATOR, 0, 0, 0, 1, "el01_open" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "el02_panel" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "el03_back", "elevator.back" },
    { DEV_AUTOTEST_ACTION_OPEN_ELEVATOR, 0, 0, 0, 1, "el04_open_again" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "el05_panel_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 12, "el06_travelling", "elevator.level.2" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 200, "el07_arrived" },
};

// Scripts reading the mouse with touch-native input (touch architecture
// step 3): after a tap on the ground and a long press on a critter the
// pointer is where the finger was, the click hook ran for both (pressed and
// released), the long press's button is held while the finger is down.
static const DevAutotestStep kDevAutotestModBridgeSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "b00_start" },
    { DEV_AUTOTEST_ACTION_LOG_POINTER, 0, 0, 0, 1, "b01_before" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 3, ROTATION_SE, 0, 60, "b02_tap_ground" },
    { DEV_AUTOTEST_ACTION_LOG_POINTER, 0, 0, 0, 1, "b03_after_tap" },
    { DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER, 0, 0, 0, 20, "b04_long_press_critter" },
    { DEV_AUTOTEST_ACTION_LOG_POINTER, 0, 0, 0, 1, "b05_after_long_press" },
};

// Death screen: the picture with its subtitle, a tap goes on to the main
// menu.
static const DevAutotestStep kDevAutotestDeathSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "de00_start" },
    { DEV_AUTOTEST_ACTION_KILL_DUDE, 0, 0, 0, 30, "de01_death_screen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "de02_main_menu", "scene.screen" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "de03_menu_shown", "mainmenu.new" },
};

// Ending slides: two endings' conditions set, the slides with their
// subtitles, a tap skips one; then the credits roll, a tap stops them.
static const DevAutotestStep kDevAutotestEndgameSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 20, "eg00_start" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 408, 1, 0, 1, "eg01_gvar" },
    { DEV_AUTOTEST_ACTION_SET_GVAR, 700, 1, 0, 1, "eg02_gvar" },
    { DEV_AUTOTEST_ACTION_REQUEST_ENDGAME, 0, 0, 0, 90, "eg03_slide" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 120, "eg04_subtitle" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "eg05_next_slide", "scene.screen" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 200, "eg06_after_slides", "scene.screen" },
};

// Called shot panel of the mobile UI (aimed attack at the nearest critter).
static const DevAutotestStep kDevAutotestCalledShotSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "s00_start" },
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "s00b_smg" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 5, 0, 0, 20, "s00c_near_critter" },
    { DEV_AUTOTEST_ACTION_HUD_TAP_MODE, 1, 0, 0, 20, "s01_aimed_mode" },
    { DEV_AUTOTEST_ACTION_LOG_HIT_MODE, 0, 0, 0, 1, "s02_hit_mode" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "s03_combat" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 20, "s03b_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 40, "s04_select_target" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "s05_panel" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "s06_cancelled", "calledshot.cancel" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 30, "s07_panel_again" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 120, "s08_head", "calledshot.0" },
    { DEV_AUTOTEST_ACTION_LOG_COMBAT, 0, 0, 0, 1, "s09_after_attack" },
};

// Combat speed: dude walks, runs and punches in combat (4 hexes there and
// back, the time logged when each ends) with the game's rule, with
// `combat_speed_all_animations`, and so on a slow device (animations catch
// up on frames the game's loop was late for).
#define COMBAT_SPEED_ROUND(prefix, speed, all)                                                          \
    { DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED, speed, 1, all, 1, prefix "_settings" },                    \
        { DEV_AUTOTEST_ACTION_SET_ACTION_POINTS, 99, 0, 0, 1, prefix "_ap" },                          \
        { DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION, 0, 4, ROTATION_SW, 200, prefix "_walk" },           \
        { DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION, 1, 4, ROTATION_NE, 200, prefix "_run" },            \
        { DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION, 2, ANIM_THROW_PUNCH, 0, 120, prefix "_punch" },         \
        { DEV_AUTOTEST_ACTION_LOG_ANIMATION_SOUND, 0, 0, 0, 1, prefix "_burst_sound", "WAH2XXX2" }

static const DevAutotestStep kDevAutotestCombatSpeedSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "v00_start" },
    { DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER, 8, 0, 0, 20, "v01_near_critter" },
    { DEV_AUTOTEST_ACTION_START_COMBAT, 0, 0, 0, 60, "v02_combat" },
    COMBAT_SPEED_ROUND("v10_slowest", 0, 0),
    COMBAT_SPEED_ROUND("v20_game", 50, 0),
    COMBAT_SPEED_ROUND("v30_all", 50, 1),
    { DEV_AUTOTEST_ACTION_SLOW_FRAMES, 50, 0, 0, 1, "v40_slow_device" },
    COMBAT_SPEED_ROUND("v41_all_slow", 50, 1),
    { DEV_AUTOTEST_ACTION_SLOW_FRAMES, 0, 0, 0, 1, "v50_normal_device" },
    { DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED, -1, 0, 0, 1, "v51_settings_back" },
};

#undef COMBAT_SPEED_ROUND

// Camera over a loaded save (`--dev-load-game=<slot>`: the test save in
// Arroyo on the Mac, the player's on a device): dragging at 1x and zoomed out
// to the map's edges, pinching, walking, stepping under a roof and out; each
// part's frame time, map pixels drawn and uploaded to the GPU logged
// (PERF_LOG after each). A benchmark: compare before and after changes.
static const DevAutotestStep kDevAutotestPerfMapSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "m00_start" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 60, "m02_start" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m03_idle" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, 12, 0, 60, 62, "m04_drag_right" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, 0, 9, 60, 62, "m05_drag_down" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m06_drag_1x" },
    { DEV_AUTOTEST_ACTION_PINCH, 0.4f, 0.2f, 0.0f, 20, "m07_pinch_in" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m08_pinch" },
    { DEV_AUTOTEST_ACTION_ZOOM, 0.5f, -1, -1, 20, "m09_zoom_out" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m10_settle" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, -16, -12, 90, 92, "m11_drag_to_edge" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m12_drag_0.5x_edge" },
    { DEV_AUTOTEST_ACTION_PAN_FRAMES, 16, 12, 90, 92, "m13_drag_back" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m14_drag_0.5x" },
    { DEV_AUTOTEST_ACTION_ZOOM, 1.0f, -1, -1, 20, "m15_zoom_in" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m16_settle" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 150, "m17_walk" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m18_walk" },
    { DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF, 0, 0, 0, 20, "m19_under_roof" },
    { DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF, 1, 0, 0, 20, "m20_out_of_roof" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m21_roofs" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 60, "m19_idle_again" },
    { DEV_AUTOTEST_ACTION_PERF_LOG, 0, 0, 0, 1, "m20_idle" },
};

// A finger down on the map, the game menu opens under it (Esc: Android's
// back gesture), the finger is lifted over the menu: the map must forget it,
// or every later touch is its second finger (touch looked dead or jumped to
// where it was). After closing the menu a tap on the map walks.
static const DevAutotestStep kDevAutotestStuckFingerSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "f00_start" },
    { DEV_AUTOTEST_ACTION_FINGER, 0, 0.35f, 0.3f, 5, "f01_finger_down" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 30, "f02_menu" },
    { DEV_AUTOTEST_ACTION_FINGER, 1, 0.35f, 0.3f, 10, "f03_finger_up" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "f04_continue", "menu.continue" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE, 4, ROTATION_SW, 0, 120, "f05_tap_walk" },
    { DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED, 0, 0, 0, 1, "f06_moved" },
};

// A tap on the HUD's menu button read after the game stalled 600 ms is too
// old: dropped, the menu doesn't open. A tap right after works.
static const DevAutotestStep kDevAutotestStaleTouchSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "g00_start" },
    { DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL, 600, 0, 0, 20, "g01_tap_during_stall", "hud.0" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 0, 0, 0, 1, "g02_menu_not_opened", "menu.continue" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "g03_tap", "hud.0" },
    { DEV_AUTOTEST_ACTION_CHECK_WIDGET, 1, 0, 0, 1, "g04_menu_opened", "menu.continue" },
};

// Armor off in the inventory: the dude on the map looks without it once the
// inventory closes (not only after opening it again).
static const DevAutotestStep kDevAutotestArmorLookSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "a00_start" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 10, "a01_jacket" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 1.6f, 0, 0, 20, "a01b_zoom" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a02_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a03_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a04_closed_wearing" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a05_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a06_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a07_closed_without" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a08_open_again" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a09_closed_again" },
    // With a weapon in hand, the inventory left through the map tab.
    { DEV_AUTOTEST_ACTION_EQUIP_ITEM, 9, HAND_LEFT, 0, 20, "a10_smg" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a11_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a12_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a13_closed_wearing" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a14_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a15_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 150, "a16_map_tab", "inventory.map" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a17_back_on_map" },
    // Worn again, then dropped on the ground from its slot.
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a18_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "a19_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a20_closed_wearing" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "a21_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 60, "a22_drop", "inventory.slot.armor", "inventory.info" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "a23_closed_dropped" },
};

// Armor changed while the dude walks: the inventory can't stop a walk (it is
// a reserved animation), the walk goes on after it and must not put the old
// look back (2026-10-01: the dude kept the armor's look after taking it off).
static const DevAutotestStep kDevAutotestArmorWalkSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "w00_start" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 10, "w01_jacket" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 1.6f, 0, 0, 20, "w02_zoom" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 4, "w03_walk" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "w04_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "w05_wear", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 200, "w06_walked_wearing" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 4, "w07_walk" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "w08_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "w09_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 200, "w10_walked_without" },
};

// Armor changes between quick saves and quick loads, some while the dude
// walks: the look on the map stays what he wears ("look ... ok" lines).
static const DevAutotestStep kDevAutotestArmorSaveLoadSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "l00_start" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 5, "l01_jacket" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 1, 0, 0, 5, "l02_leather" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l03_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l04_wear_jacket", "inventory.pid.74", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l05_closed" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 30, "l06_saved_jacket" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l07_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l08_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l09_closed_without" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l10_loaded_jacket" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l11_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l12_wear_leather", "inventory.pid.1", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l13_closed_leather" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l14_loaded_jacket" },
    { DEV_AUTOTEST_ACTION_WALK, 8, 0, 0, 4, "l15_walk" },
    { DEV_AUTOTEST_ACTION_QUICK_SAVE, 0, 0, 0, 120, "l16_saved_walking" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l17_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l18_take_off", "inventory.slot.armor", "inventory.list" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l19_closed_without" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l20_loaded_walking_save" },
    { DEV_AUTOTEST_ACTION_WALK, 6, 0, 0, 4, "l21_walk" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 120, "l22_loaded_while_walking" },
    { DEV_AUTOTEST_ACTION_HUD_TAP, HUD_ELEMENT(QuickSave), 0, 0, 60, "l23_hud_save" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "l24_open" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 30, "l25_wear_leather", "inventory.pid.1", "inventory.slot.armor" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "l26_closed_leather" },
    { DEV_AUTOTEST_ACTION_QUICK_LOAD, 0, 0, 0, 60, "l27_loaded" },
};

// Party members' items on the barter table go back to them when the barter
// is called off (`barterReturnPlayerTable`): Sulik's jacket that he can't
// carry anymore (the dude gave him his own meanwhile) goes to the dude, his
// rock goes back to him; a party member far away can't be switched to.
static const DevAutotestStep kDevAutotestBarterPartySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "p00_map" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 27, ROTATION_NW, 10, "p01_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_LOG_CRITTERS, 0, 0, 0, 1, "p01b_critters" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 74, 0, 0, 1, "p02_sulik_jacket" },
    { DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT, 19, 2, 0, 1, "p03_sulik_rocks" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 0, 0, 1, "p04_dude_jacket" },
    { DEV_AUTOTEST_ACTION_CENTER_CRITTER, 0, 0, 0, 10, "p05_center_critter" },
    { DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER, 0, 0, 0, 300, "p06_talk" },
    { DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER, 2, 0, 0, 10, "p06b_sulik_near" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p07_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p08_sulik_tab", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "p09_offer_jacket", "barter.player.pid.74", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p10_dude_tab", "barter.party.0" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "p11_jacket_to_sulik", "barter.player.pid.74", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p12_called_off", "dialog.tab.talk" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p13_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "p14_sulik_tab", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_DRAG, 0, 0, 0, 40, "p15_offer_rock", "barter.player.pid.19", "barter.offer" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "p16_quantity_done", "quantity.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p17_called_off", "dialog.tab.talk" },
    { DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER, 40, 0, 0, 10, "p18_sulik_away" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p19_barter", "dialog.tab.barter" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 60, "p20_far_tab", "barter.party.1" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 90, "p21_talk", "dialog.tab.talk" },
};

// The game's floating texts over the map at a fixed size (mui_floating_text.cc):
// over the dude's head at zoom 1, 0.5 and 3, kept on screen near an edge.
static const DevAutotestStep kDevAutotestFloatTextSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "f00_start" },
    { DEV_AUTOTEST_ACTION_FLOAT_TEXT, 1, 0, 0, 20, "f01_text", "\xcf\xf0\xe8\xe2\xe5\xf2, \xed\xe5\xe7\xed\xe0\xea\xee\xec\xe5\xf6! Long words wrap into lines over the speaker's head." },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 0.5f, 0, 0, 20, "f02_zoom50" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 3.0f, 0, 0, 20, "f03_zoom300" },
    { DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE, 1.0f, 0, 0, 20, "f04_zoom100" },
    { DEV_AUTOTEST_ACTION_PAN, 0, -260, 0, 20, "f05_dude_near_top" },
};

// Logs the state (items, the dude's look, the HUD) of a loaded game
// (--dev-load-game=<slot>): for checking a player's save.
static const DevAutotestStep kDevAutotestStateSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "t00_state" },
    { DEV_AUTOTEST_ACTION_COMMAND, static_cast<int>(GameCommandType::Inventory), 0, 0, 120, "t01_inventory" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 60, "t02_closed" },
};

// Klamath's trapper camp and its rat caves with Sulik in the party: down,
// up and down again (the caves then load from the saved map). On the phone
// such a return crashed (2026-10-01): the touch HUD drawn while the map's
// file was read read protos, which broke the read's progress count (see
// `gFileReadProgressReporting` in db.cc). Run with --dev-map=klatrap.map.
static const DevAutotestStep kDevAutotestMapReturnSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "r00_trappers" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "r01_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r02_caves", "klaratcv.map" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r03_up", "klatrap.map" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r04_caves_again", "klaratcv.map" },
    { DEV_AUTOTEST_ACTION_MAP_TRANSITION, 0, -1, 0, 120, "r05_up_again", "klatrap.map" },
};

// Touch recorder: very short taps at the left edge (a system edge gesture
// re-sending touches looks like that) and a tap read late are dumped to
// touch.log.
static const DevAutotestStep kDevAutotestTouchLogSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "k00_start" },
    { DEV_AUTOTEST_ACTION_TOUCH_LOG, 0, 0, 0, 1, "k01_recorder_on" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.5f, 10, "k02_tap" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.52f, 10, "k03_tap" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.5f, 10, "k04_tap" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.01f, 0.48f, 10, "k05_tap" },
    { DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL, 600, 0, 0, 240, "k06_tap_during_stall", "hud.0" },
    { DEV_AUTOTEST_ACTION_FINGER, 2, 0.5f, 0.3f, 10, "k07_tap" },
    { DEV_AUTOTEST_ACTION_TOUCH_LOG, 2, 0, 0, 1, "k08_game_closed" },
    { DEV_AUTOTEST_ACTION_TOUCH_LOG, 1, 0, 1, 1, "k09_dumps", "burst of very short taps", "touches read late" },
};

// Mobile character screen in creation mode (over the new game's dude).
static const DevAutotestStep kDevAutotestCreationSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "n00_start" },
    { DEV_AUTOTEST_ACTION_OPEN_CHARACTER_CREATION, 0, 0, 0, 40, "n01_open" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 10, "n01b_wait" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 10, "n02_str_plus", "character.special.0.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n03_luck_minus", "character.special.6.minus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n04_skills", "character.tab.skills" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n05_tag", "character.skills.0.tag" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n06_traits", "character.tab.perks" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n07_trait", "character.traits.1.toggle" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n08_name", "character.name" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_BACKSPACE, 0, 0, 5, "n08b_erase" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_BACKSPACE, 0, 0, 5, "n08c_erase" },
    { DEV_AUTOTEST_ACTION_KEY, SDL_SCANCODE_BACKSPACE, 0, 0, 5, "n08d_erase" },
    { DEV_AUTOTEST_ACTION_TYPE_TEXT, 0, 0, 0, 20, "n08e_typed", "\xd0\x9d\xd0\xb0\xd1\x80\xd0\xb5\xd0\xba Q" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n09_name_done", "character.name.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n10_age", "character.age.plus" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "n11_gender", "character.gender" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 40, "n12_done", "character.done" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "n13_dialog_closed", "dialog.primary" },
    { DEV_AUTOTEST_ACTION_BACK, 0, 0, 0, 40, "n14_cancelled" },
};

// Party member control tab of the mobile talk screen (test save, Sulik).
static const DevAutotestStep kDevAutotestPartySteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "q00_start" },
    { DEV_AUTOTEST_ACTION_LOAD_GAME, 9, 0, 0, 90, "q01_loaded" },
    { DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER, 0, 0, 0, 150, "q02_talk" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 120, "q02b_wait" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q03_party_tab", "dialog.tab.party" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q03b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q04_berserk", "talk.party.disposition.4" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q04b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q05_distance", "talk.party.setting.3" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q06_charge", "talk.party.values.1" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q06b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 30, "q07_best_weapon", "talk.party.weapon" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL, 0, 0, 0, 1, "q07b_log" },
    { DEV_AUTOTEST_ACTION_MUI_TAP, 0, 0, 0, 20, "q08_talk_tab", "dialog.tab.talk" },
};

static const DevAutotestStep kDevAutotestTestSaveSteps[] = {
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 60, "s00_village" },
    { DEV_AUTOTEST_ACTION_SET_NAME, 0, 0, 0, 1, "s01_name", "\xD2\xE5\xF1\xF2" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 8, 1, 0, 1, "s02_give_8" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 9, 1, 0, 1, "s02_give_9" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 10, 1, 0, 1, "s02_give_10" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 6, 1, 0, 1, "s02_give_6" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 4, 1, 0, 1, "s02_give_4" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 29, 60, 0, 1, "s02_give_29" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 30, 30, 0, 1, "s02_give_30" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 34, 40, 0, 1, "s02_give_34" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 1, 1, 0, 1, "s02_give_1" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 74, 1, 0, 1, "s02_give_74" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 40, 6, 0, 1, "s02_give_40" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 144, 2, 0, 1, "s02_give_144" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 273, 3, 0, 1, "s02_give_273" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 109, 2, 0, 1, "s02_give_109" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 48, 1, 0, 1, "s02_give_48" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 87, 1, 0, 1, "s02_give_87" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 259, 2, 0, 1, "s02_give_259" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 110, 1, 0, 1, "s02_give_110" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 49, 1, 0, 1, "s02_give_49" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 47, 1, 0, 1, "s02_give_47" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 84, 1, 0, 1, "s02_give_84" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 127, 1, 0, 1, "s02_give_127" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 79, 3, 0, 1, "s02_give_79" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 51, 2, 0, 1, "s02_give_51" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 52, 1, 0, 1, "s02_give_52" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 106, 4, 0, 1, "s02_give_106" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 73, 1, 0, 1, "s02_give_73" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 90, 1, 0, 1, "s02_give_90" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 46, 1, 0, 1, "s02_give_46" },
    { DEV_AUTOTEST_ACTION_GIVE_ITEM, 41, 750, 0, 1, "s02_give_41" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 97, 2, ROTATION_SE, 10, "s03_sulik", "Kcsulik.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 6, 1, 0, 1, "s03b_sulik_sledgehammer" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 273, 2, 0, 1, "s03c_sulik_powder" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 6, 0, 0, 1, "s03d_sulik_wields" },
    { DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER, 62, 2, ROTATION_SW, 10, "s04_vic", "DCVic.int" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 8, 1, 0, 1, "s04b_vic_pistol" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 29, 24, 0, 1, "s04c_vic_ammo" },
    { DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM, 84, 1, 0, 1, "s04d_vic_lockpicks" },
    { DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM, 8, 0, 0, 1, "s04e_vic_wields" },
    { DEV_AUTOTEST_ACTION_NONE, 0, 0, 0, 30, "s05_ready" },
    { DEV_AUTOTEST_ACTION_LOG_INVENTORY, 0, 0, 0, 1, "s05b_log" },
    { DEV_AUTOTEST_ACTION_LOG_PARTY, 0, 0, 0, 1, "s05c_party" },
    { DEV_AUTOTEST_ACTION_SAVE_GAME, 9, 0, 0, 30, "s06_save", "\xD2\xC5\xD1\xD2: \xE8\xED\xE2\xE5\xED\xF2\xE0\xF0\xFC, 2 \xF1\xEF\xF3\xF2\xED\xE8\xEA\xE0" },
};

// Scenarios changing saves run in a SAVEGAME of their own: the test game's
// saves wait in "SAVEGAME.autotest" meanwhile and come back when the test
// exits (after a run that was killed - when the next one starts).
enum class DevAutotestSaves {
    // The test game's saves.
    Keep,
    // No saves.
    Empty,
    // Only a copy of the test save (slot 10) in slot 1.
    TestSaveInSlot1,
    // The same, its SAVE.DAT cut after the header and the thumbnail (a
    // save that can't be loaded).
    BrokenTestSaveInSlot1,
    // Copies of the test save in slots 1, 11-20 and 21-25.
    QuickRing,
};

static DevAutotestSaves gDevAutotestSaves = DevAutotestSaves::Keep;

static std::string devAutotestSaveRoot()
{
    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\SAVEGAME", settings.system.master_patches_path.c_str());
    compat_windows_path_to_native(path);
    compat_resolve_path(path);
    return path;
}

static void devAutotestRestoreSaves()
{
    std::string root = devAutotestSaveRoot();
    std::string kept = root + ".autotest";

    saveStorage::Info info;
    if (!saveStorage::inspect(kept, info) || !info.exists) {
        return;
    }

    if (saveStorage::removeTree(root) && saveStorage::move(kept, root)) {
        devAutotestLog("  test game's saves restored\n");
    } else {
        devAutotestLog("FAIL: test game's saves are left in %s\n", kept.c_str());
    }
}

static void devAutotestPrepareSaves()
{
    devAutotestRestoreSaves();

    if (gDevAutotestSaves != DevAutotestSaves::Keep) {
        std::string root = devAutotestSaveRoot();
        std::string kept = root + ".autotest";
        bool prepared = saveStorage::move(root, kept) && saveStorage::makeDirectory(root);
        if (prepared && gDevAutotestSaves != DevAutotestSaves::Empty) {
            prepared = saveStorage::copyTree(kept + "/SLOT10", root + "/SLOT01");
        }

        for (int slot = 11; prepared && gDevAutotestSaves == DevAutotestSaves::QuickRing && slot <= 25; slot++) {
            char name[32];
            snprintf(name, sizeof(name), "/SLOT%02d", slot);
            prepared = saveStorage::copyTree(kept + "/SLOT10", root + name);
        }

        if (prepared && gDevAutotestSaves == DevAutotestSaves::BrokenTestSaveInSlot1) {
            std::string path = root + "/SLOT01/SAVE.DAT";
            std::string data;
            FILE* stream = fopen(path.c_str(), "rb");
            if (stream != nullptr) {
                data.resize(40000);
                data.resize(fread(&data[0], 1, data.size(), stream));
                fclose(stream);
            }
            prepared = data.size() == 40000 && saveStorage::writeFile(path, data);
        }

        atexit(devAutotestRestoreSaves);

        if (!prepared) {
            devAutotestLog("FAIL: own SAVEGAME not prepared\n");
            exit(EXIT_FAILURE);
        }

        devAutotestLog("  own SAVEGAME%s\n", gDevAutotestSaves == DevAutotestSaves::Empty ? ", empty"
                : gDevAutotestSaves == DevAutotestSaves::QuickRing ? " with the test save in slots 1, 11-25"
                                                                   : " with the test save in slot 1");
    }

    lsgMobileRefreshSlots();
}

void devAutotestSetScenario(const char* name)
{
    if (strcmp(name, "worldmap") == 0) {
        gDevAutotestSteps = kDevAutotestWorldmapSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestWorldmapSteps) / sizeof(kDevAutotestWorldmapSteps[0]);
    } else if (strcmp(name, "loadsave") == 0) {
        gDevAutotestSteps = kDevAutotestLoadSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestLoadSaveSteps) / sizeof(kDevAutotestLoadSaveSteps[0]);
    } else if (strcmp(name, "savehistory") == 0) {
        gDevAutotestSteps = kDevAutotestSaveHistorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestSaveHistorySteps) / sizeof(kDevAutotestSaveHistorySteps[0]);
        gDevAutotestSaves = DevAutotestSaves::Empty;
    } else if (strcmp(name, "gamefiles") == 0) {
        gDevAutotestSteps = kDevAutotestGameFilesSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestGameFilesSteps) / sizeof(kDevAutotestGameFilesSteps[0]);
    } else if (strcmp(name, "preferences") == 0) {
        gDevAutotestSteps = kDevAutotestPreferencesSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPreferencesSteps) / sizeof(kDevAutotestPreferencesSteps[0]);
    } else if (strcmp(name, "muiloadsave") == 0) {
        gDevAutotestSteps = kDevAutotestMuiLoadSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiLoadSaveSteps) / sizeof(kDevAutotestMuiLoadSaveSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::Empty;
    } else if (strcmp(name, "quicksaves") == 0) {
        gDevAutotestSteps = kDevAutotestQuickSavesSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestQuickSavesSteps) / sizeof(kDevAutotestQuickSavesSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::QuickRing;
    } else if (strcmp(name, "muiloadsavedelete") == 0) {
        gDevAutotestSteps = kDevAutotestMuiDeleteSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiDeleteSaveSteps) / sizeof(kDevAutotestMuiDeleteSaveSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::TestSaveInSlot1;
    } else if (strcmp(name, "muiloadfailed") == 0) {
        gDevAutotestSteps = kDevAutotestMuiLoadFailedSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiLoadFailedSteps) / sizeof(kDevAutotestMuiLoadFailedSteps[0]);
        gDevAutotestSaves = DevAutotestSaves::BrokenTestSaveInSlot1;
    } else if (strcmp(name, "muiloadsavemain") == 0) {
        gDevAutotestSteps = kDevAutotestMuiMainMenuLoadSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiMainMenuLoadSteps) / sizeof(kDevAutotestMuiMainMenuLoadSteps[0]);
    } else if (strcmp(name, "muiloadsavemainresume") == 0) {
        gDevAutotestSteps = kDevAutotestMuiMainMenuResumeSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiMainMenuResumeSteps) / sizeof(kDevAutotestMuiMainMenuResumeSteps[0]);
    } else if (strcmp(name, "muiloadsavemainempty") == 0) {
        gDevAutotestSteps = kDevAutotestMuiMainMenuEmptySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMuiMainMenuEmptySteps) / sizeof(kDevAutotestMuiMainMenuEmptySteps[0]);
        gDevAutotestSaves = DevAutotestSaves::Empty;
    } else if (strcmp(name, "pccharacter") == 0) {
        gDevAutotestSteps = kDevAutotestPcCharacterSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcCharacterSteps) / sizeof(kDevAutotestPcCharacterSteps[0]);
    } else if (strcmp(name, "character") == 0) {
        gDevAutotestSteps = kDevAutotestCharacterSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCharacterSteps) / sizeof(kDevAutotestCharacterSteps[0]);
    } else if (strcmp(name, "combatscreens") == 0) {
        gDevAutotestSteps = kDevAutotestCombatScreensSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCombatScreensSteps) / sizeof(kDevAutotestCombatScreensSteps[0]);
    } else if (strcmp(name, "pcpipboy") == 0) {
        gDevAutotestSteps = kDevAutotestPcPipboySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcPipboySteps) / sizeof(kDevAutotestPcPipboySteps[0]);
    } else if (strcmp(name, "pipboy") == 0) {
        gDevAutotestSteps = kDevAutotestPipboySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPipboySteps) / sizeof(kDevAutotestPipboySteps[0]);
    } else if (strcmp(name, "automap") == 0) {
        gDevAutotestSteps = kDevAutotestAutomapSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestAutomapSteps) / sizeof(kDevAutotestAutomapSteps[0]);
    } else if (strcmp(name, "movie") == 0) {
        gDevAutotestSteps = kDevAutotestMovieSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMovieSteps) / sizeof(kDevAutotestMovieSteps[0]);
    } else if (strcmp(name, "titleintro") == 0) {
        gDevAutotestSteps = kDevAutotestTitleIntroSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTitleIntroSteps) / sizeof(kDevAutotestTitleIntroSteps[0]);
    } else if (strcmp(name, "modbridge") == 0) {
        gDevAutotestSteps = kDevAutotestModBridgeSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestModBridgeSteps) / sizeof(kDevAutotestModBridgeSteps[0]);
    } else if (strcmp(name, "death") == 0) {
        gDevAutotestSteps = kDevAutotestDeathSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestDeathSteps) / sizeof(kDevAutotestDeathSteps[0]);
    } else if (strcmp(name, "endgame") == 0) {
        gDevAutotestSteps = kDevAutotestEndgameSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestEndgameSteps) / sizeof(kDevAutotestEndgameSteps[0]);
    } else if (strcmp(name, "elevator") == 0) {
        gDevAutotestSteps = kDevAutotestElevatorSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestElevatorSteps) / sizeof(kDevAutotestElevatorSteps[0]);
    } else if (strcmp(name, "mainmenu") == 0) {
        gDevAutotestSteps = kDevAutotestMainMenuSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMainMenuSteps) / sizeof(kDevAutotestMainMenuSteps[0]);
    } else if (strcmp(name, "newgameelder") == 0) {
        gDevAutotestSteps = kDevAutotestNewGameElderSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestNewGameElderSteps) / sizeof(kDevAutotestNewGameElderSteps[0]);
    } else if (strcmp(name, "pipboynotworn") == 0) {
        gDevAutotestSteps = kDevAutotestPipboyNotWornSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPipboyNotWornSteps) / sizeof(kDevAutotestPipboyNotWornSteps[0]);
    } else if (strcmp(name, "armorlook") == 0) {
        gDevAutotestSteps = kDevAutotestArmorLookSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestArmorLookSteps) / sizeof(kDevAutotestArmorLookSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "armorwalk") == 0) {
        gDevAutotestSteps = kDevAutotestArmorWalkSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestArmorWalkSteps) / sizeof(kDevAutotestArmorWalkSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "armorsaveload") == 0) {
        gDevAutotestSteps = kDevAutotestArmorSaveLoadSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestArmorSaveLoadSteps) / sizeof(kDevAutotestArmorSaveLoadSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "barterparty") == 0) {
        gDevAutotestSteps = kDevAutotestBarterPartySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBarterPartySteps) / sizeof(kDevAutotestBarterPartySteps[0]);
        gDevAutotestTraceItems = true;
        // Party members on the player side (off in the test game's config;
        // runtest.sh puts the config back).
        settings.qol.party_loot_and_barter = true;
    } else if (strcmp(name, "floattext") == 0) {
        gDevAutotestSteps = kDevAutotestFloatTextSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestFloatTextSteps) / sizeof(kDevAutotestFloatTextSteps[0]);
    } else if (strcmp(name, "state") == 0) {
        gDevAutotestSteps = kDevAutotestStateSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestStateSteps) / sizeof(kDevAutotestStateSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "mapreturn") == 0) {
        gDevAutotestSteps = kDevAutotestMapReturnSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMapReturnSteps) / sizeof(kDevAutotestMapReturnSteps[0]);
    } else if (strcmp(name, "touchlog") == 0) {
        gDevAutotestSteps = kDevAutotestTouchLogSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTouchLogSteps) / sizeof(kDevAutotestTouchLogSteps[0]);
    } else if (strcmp(name, "staletouch") == 0) {
        gDevAutotestSteps = kDevAutotestStaleTouchSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestStaleTouchSteps) / sizeof(kDevAutotestStaleTouchSteps[0]);
    } else if (strcmp(name, "stuckfinger") == 0) {
        gDevAutotestSteps = kDevAutotestStuckFingerSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestStuckFingerSteps) / sizeof(kDevAutotestStuckFingerSteps[0]);
    } else if (strcmp(name, "perfmap") == 0) {
        gDevAutotestSteps = kDevAutotestPerfMapSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPerfMapSteps) / sizeof(kDevAutotestPerfMapSteps[0]);
    } else if (strcmp(name, "combatspeed") == 0) {
        gDevAutotestSteps = kDevAutotestCombatSpeedSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCombatSpeedSteps) / sizeof(kDevAutotestCombatSpeedSteps[0]);
    } else if (strcmp(name, "calledshot") == 0) {
        gDevAutotestSteps = kDevAutotestCalledShotSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCalledShotSteps) / sizeof(kDevAutotestCalledShotSteps[0]);
    } else if (strcmp(name, "creation") == 0) {
        gDevAutotestSteps = kDevAutotestCreationSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCreationSteps) / sizeof(kDevAutotestCreationSteps[0]);
    } else if (strcmp(name, "party") == 0) {
        gDevAutotestSteps = kDevAutotestPartySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPartySteps) / sizeof(kDevAutotestPartySteps[0]);
    } else if (strcmp(name, "pcparty") == 0) {
        gDevAutotestSteps = kDevAutotestPcPartySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcPartySteps) / sizeof(kDevAutotestPcPartySteps[0]);
    } else if (strcmp(name, "testsave") == 0) {
        gDevAutotestSteps = kDevAutotestTestSaveSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTestSaveSteps) / sizeof(kDevAutotestTestSaveSteps[0]);
    } else if (strcmp(name, "loot") == 0) {
        gDevAutotestSteps = kDevAutotestLootSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestLootSteps) / sizeof(kDevAutotestLootSteps[0]);
    } else if (strcmp(name, "pcbar") == 0) {
        gDevAutotestSteps = kDevAutotestPcBarSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcBarSteps) / sizeof(kDevAutotestPcBarSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "hudtrace") == 0) {
        gDevAutotestSteps = kDevAutotestHudSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestHudSteps) / sizeof(kDevAutotestHudSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "combattrace") == 0) {
        gDevAutotestSteps = kDevAutotestCombatScreensSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCombatScreensSteps) / sizeof(kDevAutotestCombatScreensSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "calledtrace") == 0) {
        gDevAutotestSteps = kDevAutotestCalledShotSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestCalledShotSteps) / sizeof(kDevAutotestCalledShotSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "pcinventory") == 0) {
        gDevAutotestSteps = kDevAutotestPcInventorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestPcInventorySteps) / sizeof(kDevAutotestPcInventorySteps[0]);
    } else if (strcmp(name, "inventorytrace") == 0) {
        // The inventory scenario, with everything it changes after every step.
        gDevAutotestSteps = kDevAutotestInventorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestInventorySteps) / sizeof(kDevAutotestInventorySteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "loottrace") == 0) {
        gDevAutotestSteps = kDevAutotestLootSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestLootSteps) / sizeof(kDevAutotestLootSteps[0]);
        gDevAutotestTraceItems = true;
    } else if (strcmp(name, "inventory") == 0) {
        gDevAutotestSteps = kDevAutotestInventorySteps;
        gDevAutotestStepCount = sizeof(kDevAutotestInventorySteps) / sizeof(kDevAutotestInventorySteps[0]);
    } else if (strcmp(name, "bartertrace") == 0) {
        gDevAutotestSteps = kDevAutotestBarterTraceSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBarterTraceSteps) / sizeof(kDevAutotestBarterTraceSteps[0]);
    } else if (strcmp(name, "headtrace") == 0) {
        gDevAutotestSteps = kDevAutotestHeadTraceSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestHeadTraceSteps) / sizeof(kDevAutotestHeadTraceSteps[0]);
    } else if (strcmp(name, "dialogtrace") == 0) {
        gDevAutotestSteps = kDevAutotestDialogTraceSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestDialogTraceSteps) / sizeof(kDevAutotestDialogTraceSteps[0]);
    } else if (strcmp(name, "barter") == 0) {
        gDevAutotestSteps = kDevAutotestBarterSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestBarterSteps) / sizeof(kDevAutotestBarterSteps[0]);
    } else if (strcmp(name, "talk") == 0) {
        gDevAutotestSteps = kDevAutotestTalkSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestTalkSteps) / sizeof(kDevAutotestTalkSteps[0]);
    } else if (strcmp(name, "ui") == 0) {
        gDevAutotestSteps = kDevAutotestUiSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestUiSteps) / sizeof(kDevAutotestUiSteps[0]);
    } else if (strcmp(name, "magnet") == 0) {
        gDevAutotestSteps = kDevAutotestMagnetSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestMagnetSteps) / sizeof(kDevAutotestMagnetSteps[0]);
    } else if (strcmp(name, "hud") == 0) {
        gDevAutotestSteps = kDevAutotestHudSteps;
        gDevAutotestStepCount = sizeof(kDevAutotestHudSteps) / sizeof(kDevAutotestHudSteps[0]);
    }
}

void devAutotestInit(const char* outputPath)
{
    gDevAutotestEnabled = true;
    gDevAutotestOutputPath = outputPath;

    // On a device the game makes it (folders adb makes aren't writable for
    // the app).
    saveStorage::makeDirectory(gDevAutotestOutputPath);

    devAutotestLog("autotest started, zoom range %.2f..%.2f, filter %d\n",
        settings.world_view.zoom_min,
        settings.world_view.zoom_max,
        settings.world_view.filter);
}

bool devAutotestIsEnabled()
{
    return gDevAutotestEnabled;
}

// FNV-1a of [count] values.
static unsigned int devAutotestChecksum(const int* values, int count)
{
    unsigned int hash = 2166136261u;
    for (int index = 0; index < count; index++) {
        unsigned int value = static_cast<unsigned int>(values[index]);
        for (int byte = 0; byte < 4; byte++) {
            hash ^= (value >> (byte * 8)) & 0xFF;
            hash *= 16777619u;
        }
    }
    return hash;
}

static void devAutotestLogDialog()
{
    bool inDialog = GameMode::isInGameMode(GameMode::kDialog);
    devAutotestLog("  dialog: in dialog %d, talking %d, barter %d\n",
        inDialog ? 1 : 0, gameDialogIsTalking() ? 1 : 0, gameDialogIsInBarter() ? 1 : 0);

    if (inDialog) {
        devAutotestLog("  reply: %s\n", gameDialogGetReplyText());
        int optionCount = gameDialogGetOptionCount();
        for (int index = 0; index < optionCount; index++) {
            devAutotestLog("  option %d: %s (reaction %d)\n", index, gameDialogGetOptionText(index), gameDialogGetOptionReaction(index));
        }
    }

    devAutotestLog("  dude: caps %d, items %d, weight %d\n", itemGetTotalCaps(gDude), gDude->data.inventory.length, objectGetInventoryWeight(gDude));

    Object* speaker = inDialog ? gGameDialogSpeaker : nullptr;
    if (speaker != nullptr) {
        std::vector<int> locals;
        Script* script;
        if (scriptGetScript(speaker->sid, &script) != -1) {
            for (int index = 0; index < script->localVarsCount; index++) {
                ProgramValue value;
                if (scriptGetLocalVar(speaker->sid, index, value) == 0) {
                    locals.push_back(value.opcode);
                    locals.push_back(value.integerValue);
                }
            }
        }
        devAutotestLog("  speaker pid %d: caps %d, items %d, locals %d (%08x)\n",
            speaker->pid, itemGetTotalCaps(speaker), speaker->data.inventory.length,
            static_cast<int>(locals.size() / 2), devAutotestChecksum(locals.data(), static_cast<int>(locals.size())));
    }

    BarterView view;
    if (barterGetView(&view)) {
        devAutotestLog("  barter: offer %d, request %d, player table %d, barterer table %d\n",
            view.offerValue, view.requestValue,
            view.playerTable->data.inventory.length, view.bartererTable->data.inventory.length);
    }

    devAutotestLog("  global vars %08x, map vars %08x\n",
        devAutotestChecksum(gGameGlobalVars, gGameGlobalVarsLength),
        devAutotestChecksum(gMapGlobalVars, gMapGlobalVarsLength));
}

// Items of [owner] as "pid x quantity", with the contents of containers.
static std::string devAutotestItemsText(Object* owner)
{
    std::string text;
    Inventory* inventory = &(owner->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        Object* item = inventoryItem->item;
        char entry[64];
        snprintf(entry, sizeof(entry), " %d x%d", item->pid, inventoryItem->quantity);
        text += entry;
        if ((item->flags & OBJECT_IN_RIGHT_HAND) != 0) {
            text += "R";
        }
        if ((item->flags & OBJECT_IN_LEFT_HAND) != 0) {
            text += "L";
        }
        if ((item->flags & OBJECT_WORN) != 0) {
            text += "W";
        }
        if (itemGetType(item) == ITEM_TYPE_WEAPON) {
            snprintf(entry, sizeof(entry), "(ammo %d pid %d)", ammoGetQuantity(item), weaponGetAmmoTypeProtoId(item).pid());
            text += entry;
        }
        if (item->data.inventory.length != 0) {
            text += " [" + devAutotestItemsText(item) + " ]";
        }
    }
    return text;
}

// The interface bar's state (the touch HUD shows it).
static void devAutotestLogInterface()
{
    InterfaceItemAction actions[INTERFACE_ITEM_ACTION_COUNT];
    int count = interfaceGetAvailableItemActions(actions, INTERFACE_ITEM_ACTION_COUNT);
    std::string available;
    for (int index = 0; index < count; index++) {
        available += " " + std::to_string(static_cast<int>(actions[index]));
    }

    InterfaceItemButtonInfo info;
    bool hasInfo = interfaceGetItemButtonInfo(&info);
    int quantity = -1;
    int capacity = -1;
    interfaceGetCurrentItemAmmo(&quantity, &capacity);
    HitMode hitMode = HIT_MODE_PUNCH;
    bool aiming = false;
    int hitModeRc = interfaceGetCurrentHitMode(&hitMode, &aiming);

    devAutotestLog("  bar: enabled %d, hidden %d, end buttons %d, hand %d, action %d, available%s, hit mode %d%s, ammo %d/%d\n",
        interfaceBarEnabled() ? 1 : 0, interfaceBarIsHidden() ? 1 : 0, interfaceBarEndButtonsVisible() ? 1 : 0,
        static_cast<int>(interfaceGetCurrentHand()), static_cast<int>(interfaceGetCurrentItemAction()),
        available.empty() ? " none" : available.c_str(),
        hitModeRc == 0 ? static_cast<int>(hitMode) : -1, aiming ? " aiming" : "", quantity, capacity);
    if (hasInfo) {
        devAutotestLog("  bar item: fid %08x, text %d, aiming %d, ap %d, disabled %d\n",
            info.itemFrmId.fid(), info.textFrmId.fid(), info.aiming ? 1 : 0, info.actionPoints, info.disabled ? 1 : 0);
    }
}

// Everything the inventory and loot screens change (trace scenarios log it
// after every step).
static void devAutotestLogItems()
{
    devAutotestLog("  items:%s\n", devAutotestItemsText(gDude).c_str());

    Object* right = critterGetItem2(gDude);
    Object* left = critterGetItem1(gDude);
    Object* armor = critterGetArmor(gDude);
    devAutotestLog("  dude: right %d, left %d, armor %d, ac %d, hp %d/%d, ap %d, weight %d/%d, caps %d, fid %08x\n",
        right != nullptr ? right->pid : -1, left != nullptr ? left->pid : -1, armor != nullptr ? armor->pid : -1,
        critterGetStat(gDude, STAT_ARMOR_CLASS), critterGetHitPoints(gDude), critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS),
        gDude->data.critter.combat.ap, objectGetInventoryWeight(gDude), critterGetStat(gDude, STAT_CARRY_WEIGHT),
        itemGetTotalCaps(gDude), gDude->fid);

    // The look on the map is what the dude wears (the art; the weapon part
    // follows the hands).
    const FrmId dudeFrmId = FrmId(gDude);
    const FrmId expectedFrmId = inventoryComputeCritterFrmId(gDude, gDude->pid, right, left, armor,
        interfaceGetCurrentHand(), dudeFrmId.animationType(), dudeFrmId.rotation());
    int lookArt = static_cast<int>(dudeFrmId.frameId<CritterFrameId>());
    int expectedArt = static_cast<int>(expectedFrmId.frameId<CritterFrameId>());
    // An open inventory shows the new look only once closed.
    InventoryView inventoryView;
    bool inventoryOpen = inventoryGetView(&inventoryView);
    devAutotestLog("  look art %d, worn %d: %s\n", lookArt, expectedArt,
        lookArt == expectedArt ? "ok" : (inventoryOpen ? "inventory open" : "MISMATCH"));

    for (Object* member : get_all_party_members_objects(false)) {
        if (member == gDude) {
            continue;
        }
        Object* memberArmor = critterGetArmor(member);
        devAutotestLog("  party pid %d: armor %d, fid %08x, items:%s\n",
            member->pid, memberArmor != nullptr ? memberArmor->pid : -1, member->fid, devAutotestItemsText(member).c_str());
    }

    BarterView barter;
    if (barterGetView(&barter)) {
        devAutotestLog("  barter: party index %d, player table:%s\n", barter.partyIndex, devAutotestItemsText(barter.playerTable).c_str());
    }

    LootView loot;
    if (lootGetView(&loot)) {
        devAutotestLog("  loot target pid %d:%s\n", loot.target->pid, devAutotestItemsText(loot.target).c_str());
    }

    std::string ground;
    for (Object* object = objectFindFirstAtLocation(gDude->elevation, gDude->tile); object != nullptr; object = objectFindNextAtLocation()) {
        if (FrmId(object).objectType() == OBJ_TYPE_ITEM) {
            char entry[32];
            snprintf(entry, sizeof(entry), " %d", object->pid);
            ground += entry;
        }
    }
    devAutotestLog("  ground:%s\n", ground.empty() ? " none" : ground.c_str());
    devAutotestLog("  global vars %08x\n", devAutotestChecksum(gGameGlobalVars, gGameGlobalVarsLength));
}

void devAutotestTick()
{
    if (!gDevAutotestEnabled) {
        return;
    }

    static bool savesPrepared = false;
    if (!savesPrepared) {
        savesPrepared = true;
        devAutotestPrepareSaves();
    }

    if (gDevAutotestSlowFrameMs > 0) {
        SDL_Delay(gDevAutotestSlowFrameMs);
    }

    gDevAutotestFrames++;

    if (gDevAutotestPanFrames > 0) {
        gDevAutotestPanFrames--;
        worldViewPanBy(gDevAutotestPanX, gDevAutotestPanY);
    }

    if (gDevAutotestTimedName != nullptr && !animationIsBusy(gDude)) {
        devAutotestLog("  %s: %u ms, %d hexes\n",
            gDevAutotestTimedName,
            getTicksSince(gDevAutotestTimedStart),
            tileDistanceBetween(gDevAutotestTimedTile, gDude->tile));
        gDevAutotestTimedName = nullptr;
    }

    if (gDevAutotestStep >= gDevAutotestStepCount) {
        devAutotestLog("autotest finished\n");
        exit(EXIT_SUCCESS);
    }

    const DevAutotestStep* step = &(gDevAutotestSteps[gDevAutotestStep]);

    if (step->action == DEV_AUTOTEST_ACTION_PINCH && gDevAutotestPinchFrame < kDevAutotestPinchFrames + 1) {
        if (gDevAutotestPinchFrame == -1) {
            int mouseX;
            int mouseY;
            mouseGetPosition(&mouseX, &mouseY);
            int worldX;
            int worldY;
            worldViewScreenToWorld(mouseX, mouseY, &worldX, &worldY);
            devAutotestLog("%s: before pinch zoom %.3f, mouse (%d, %d), mouse tile %d\n", step->name, worldViewGetZoom(), mouseX, mouseY, tileFromScreenXY(worldX, worldY));
        }
        devAutotestPinchTick(step);
        return;
    }

    if (step->action == DEV_AUTOTEST_ACTION_WAIT_TALKING && !gDevAutotestStepStarted) {
        static int waited = 0;
        bool talking = gameDialogIsTalking() && gameDialogGetOptionCount() > 0;
        if (!talking && waited < static_cast<int>(step->a)) {
            waited++;
            return;
        }
        if (!talking) {
            devAutotestLog("  no talk after %d frames\n", waited);
        }
        waited = 0;
    }

    if (!gDevAutotestStepStarted) {
        gDevAutotestStepStarted = true;
        gDevAutotestDelay = step->delay;

        Rect visibleRect;
        switch (step->action) {
        case DEV_AUTOTEST_ACTION_ZOOM:
            worldViewSetZoom(step->a,
                step->b < 0 ? screenGetWidth() / 2 : step->b,
                step->c < 0 ? screenGetVisibleHeight() / 2 : step->c);
            break;
        case DEV_AUTOTEST_ACTION_ZOOM_AT_DUDE:
            if (gDude != nullptr) {
                int worldX;
                int worldY;
                tileToScreenXY(gDude->tile, &worldX, &worldY);
                int screenX;
                int screenY;
                worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
                worldViewSetZoom(step->a, screenX, screenY);
            }
            break;
        case DEV_AUTOTEST_ACTION_PAN:
            worldViewPanBy(step->a, step->b);
            break;
        case DEV_AUTOTEST_ACTION_WALK:
            if (gDude != nullptr) {
                gDevAutotestWalkTarget = tileGetTileInDirection(gDude->tile, ROTATION_SE, static_cast<int>(step->a));
                int worldX;
                int worldY;
                tileToScreenXY(gDevAutotestWalkTarget, &worldX, &worldY);
                int screenX;
                int screenY;
                worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);
                devAutotestLog("  walk from %d to %d via screen (%d, %d)\n", gDude->tile, gDevAutotestWalkTarget, screenX, screenY);
                dudeMoveToTile(gDevAutotestWalkTarget, -1);
            }
            break;
        case DEV_AUTOTEST_ACTION_CHECK_WALK:
            if (gDude != nullptr) {
                devAutotestLog("  dude at %d, target %d: %s\n", gDude->tile, gDevAutotestWalkTarget, gDude->tile == gDevAutotestWalkTarget ? "ok" : "MISMATCH");
            }
            break;
        case DEV_AUTOTEST_ACTION_CHECK_TOUCH_MOVED:
            devAutotestLog("  dude moved by touch from %d to %d\n", gDevAutotestTouchStartTile, gDude != nullptr ? gDude->tile : -1);
            if (gDude == nullptr || gDevAutotestTouchStartTile < 0 || gDude->tile == gDevAutotestTouchStartTile) {
                devAutotestLog("autotest failed: map touch did not move the player\n");
                exit(EXIT_FAILURE);
            }
            break;
        case DEV_AUTOTEST_ACTION_CENTER_DUDE:
            if (gDude != nullptr) {
                tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            }
            break;
        case DEV_AUTOTEST_ACTION_ARROW:
            _mouse_set_position(static_cast<int>(step->a), static_cast<int>(step->b));
            gameMouseSetMode(GAME_MOUSE_MODE_ARROW);
            break;
        case DEV_AUTOTEST_ACTION_TALK_PARTY_MEMBER: {
            Object* member = nullptr;
            for (Object* candidate : get_all_party_members_objects(false)) {
                if (candidate != gDude) {
                    member = candidate;
                    break;
                }
            }
            if (member != nullptr) {
                devAutotestLog("  talk to party member pid %d\n", member->pid);
                gDevAutotestPendingCapture = step->name;
                gDevAutotestStep++;
                gDevAutotestStepStarted = false;
                actionTalk(gDude, member);
                return;
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_CRITTERS:
            for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                if (object != gDude && FrmId(object).objectType() == OBJ_TYPE_CRITTER) {
                    devAutotestLog("  critter pid %d (%d): %s, tile %d, distance %d, sid %d\n",
                        object->pid, object->pid & 0xFFFFFF, objectGetName(object), object->tile,
                        objectGetDistanceBetween(gDude, object), object->sid);
                }
            }
            break;
        case DEV_AUTOTEST_ACTION_TALK_TO_PID: {
            int pid = 0x01000000 | static_cast<int>(step->a);
            Object* critter = nullptr;
            for (Object* object = objectFindFirstAtElevation(gDude->elevation); object != nullptr; object = objectFindNextAtElevation()) {
                if (object->pid == pid) {
                    critter = object;
                    break;
                }
            }
            if (critter != nullptr) {
                // Next to the critter: no walk, the same start every run.
                int tile = tileGetTileInDirection(critter->tile, ROTATION_SW, 1);
                Rect rect;
                objectSetLocation(gDude, tile, critter->elevation, &rect);
                devAutotestLog("  talk to pid %d at tile %d from tile %d\n", pid, critter->tile, tile);
                gDevAutotestPendingCapture = step->name;
                gDevAutotestStep++;
                gDevAutotestStepStarted = false;
                actionTalk(gDude, critter);
                return;
            }
            devAutotestLog("  no critter pid %d\n", pid);
            break;
        }
        case DEV_AUTOTEST_ACTION_DIALOG_BARTER:
            if (muiIsEnabled()) {
                gameDialogRequestBarter();
            } else {
                enqueueInputEvent(KEY_LOWERCASE_B);
            }
            break;
        case DEV_AUTOTEST_ACTION_BARTER_OFFER:
            if (muiIsEnabled()) {
                barterRequest(BarterRequest::Offer);
            } else {
                enqueueInputEvent(KEY_LOWERCASE_M);
            }
            break;
        case DEV_AUTOTEST_ACTION_BARTER_TALK:
            if (muiIsEnabled()) {
                barterRequest(BarterRequest::Talk);
            } else {
                enqueueInputEvent(KEY_LOWERCASE_T);
            }
            break;
        case DEV_AUTOTEST_ACTION_CHOOSE_OPTION:
            if (muiIsEnabled()) {
                gameDialogChooseOption(static_cast<int>(step->a));
            } else {
                enqueueInputEvent(KEY_1 + static_cast<int>(step->a));
            }
            break;
        case DEV_AUTOTEST_ACTION_LOG_PARTY_CONTROL: {
            PartyControlView view;
            if (gameDialogGetPartyControlView(&view)) {
                devAutotestLog("  party control: weapon pid %d, armor pid %d, hp %d/%d, disposition %d, settings %d %d %d %d %d %d\n",
                    view.weapon != nullptr ? view.weapon->pid : -1,
                    view.armor != nullptr ? view.armor->pid : -1,
                    view.hitPoints, view.maximumHitPoints, view.disposition,
                    view.settings[0], view.settings[1], view.settings[2], view.settings[3], view.settings[4], view.settings[5]);
            } else {
                devAutotestLog("  party control: none\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TYPE_TEXT: {
            SDL_Event event;
            SDL_zero(event);
            event.type = SDL_TEXTINPUT;
            SDL_strlcpy(event.text.text, step->widget, sizeof(event.text.text));
            SDL_PushEvent(&event);
            break;
        }
        case DEV_AUTOTEST_ACTION_EVENT:
            enqueueInputEvent(static_cast<int>(step->a));
            break;
        case DEV_AUTOTEST_ACTION_COMMAND:
            gameCommandPost(static_cast<GameCommandType>(static_cast<int>(step->a)));
            break;
        case DEV_AUTOTEST_ACTION_BACK:
            muiRequestBack();
            break;
        case DEV_AUTOTEST_ACTION_LOG_DIALOG:
            devAutotestLogDialog();
            break;
        case DEV_AUTOTEST_ACTION_LOG_NEW_WINDOWS: {
            // The first one takes the windows there are.
            if (!gDevAutotestStartWindowsTaken) {
                gDevAutotestStartWindowsTaken = true;
                std::string windows;
                for (int id = 1; id < 100; id++) {
                    Window* window = windowGetWindow(id);
                    if (window != nullptr) {
                        gDevAutotestStartWindows.insert(id);
                        char entry[64];
                        snprintf(entry, sizeof(entry), " %d(%dx%d%s)", id, window->width, window->height, (window->flags & WINDOW_HIDDEN) != 0 ? " hidden" : "");
                        windows += entry;
                    }
                }
                devAutotestLog("  game windows at start: %d%s\n", static_cast<int>(gDevAutotestStartWindows.size()), windows.c_str());
                break;
            }

            std::string windows;
            for (int id = 1; id < 100; id++) {
                Window* window = windowGetWindow(id);
                if (window == nullptr || gDevAutotestStartWindows.count(id) != 0) {
                    continue;
                }
                char entry[64];
                snprintf(entry, sizeof(entry), " %d(%dx%d%s)", id, window->width, window->height, (window->flags & WINDOW_HIDDEN) != 0 ? " hidden" : "");
                windows += entry;
            }
            devAutotestLog("  new game windows:%s\n", windows.empty() ? " none" : windows.c_str());
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_GVAR:
            gameSetGlobalVar(static_cast<GameGlobalVar>(static_cast<int>(step->a)), static_cast<int>(step->b));
            break;
        case DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                int tile = tileGetTileInDirection(critter->tile, ROTATION_NE, static_cast<int>(step->a));
                Rect rect;
                objectSetLocation(gDude, tile, gDude->elevation, &rect);
                tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
                devAutotestLog("  dude moved to tile %d near critter at %d\n", gDude->tile, critter->tile);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_COMBAT_SPEED: {
            static bool saved = false;
            static int combatSpeed;
            static bool playerSpeedup;
            static bool allAnimations;
            if (!saved) {
                saved = true;
                combatSpeed = settings.preferences.combat_speed;
                playerSpeedup = settings.preferences.player_speedup;
                allAnimations = settings.enhancements.combat_speed_all_animations;
            }

            if (step->a < 0) {
                settings.preferences.combat_speed = combatSpeed;
                settings.preferences.player_speedup = playerSpeedup;
                settings.enhancements.combat_speed_all_animations = allAnimations;
            } else {
                settings.preferences.combat_speed = static_cast<int>(step->a);
                settings.preferences.player_speedup = step->b != 0;
                settings.enhancements.combat_speed_all_animations = step->c != 0;
            }
            devAutotestLog("  combat speed %d, player speedup %d, all animations %d\n",
                settings.preferences.combat_speed,
                settings.preferences.player_speedup ? 1 : 0,
                settings.enhancements.combat_speed_all_animations ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_TIME_DUDE_ANIMATION: {
            int kind = static_cast<int>(step->a);
            int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(static_cast<int>(step->c)), static_cast<int>(step->b));
            int rc = -1;
            if (reg_anim_begin(ANIMATION_REQUEST_RESERVED) == 0) {
                if (kind == 2) {
                    rc = animationRegisterAnimate(gDude, static_cast<AnimationType>(static_cast<int>(step->b)), 0);
                } else if (kind == 1) {
                    rc = animationRegisterRunToTile(gDude, tile, gDude->elevation, gDude->data.critter.combat.ap, 0);
                } else {
                    rc = animationRegisterMoveToTile(gDude, tile, gDude->elevation, gDude->data.critter.combat.ap, 0);
                }
                if (reg_anim_end() != 0) {
                    rc = -1;
                }
            }

            if (rc == 0) {
                gDevAutotestTimedName = step->name;
                gDevAutotestTimedStart = getTicks();
                gDevAutotestTimedTile = gDude->tile;
            } else {
                devAutotestLog("FAIL: %s: animation not started\n", step->name);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_SLOW_FRAMES:
            gDevAutotestSlowFrameMs = static_cast<int>(step->a);
            break;
        case DEV_AUTOTEST_ACTION_STEP_UNDER_ROOF: {
            int target = -1;
            if (step->a != 0) {
                target = gDevAutotestRoofStartTile;
            } else {
                gDevAutotestRoofStartTile = gDude->tile;
                int bestDistance = INT_MAX;
                for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
                    int distance = tileDistanceBetween(gDude->tile, tile);
                    if (distance > 25 || distance >= bestDistance) {
                        continue;
                    }

                    int roofX = tile % HEX_GRID_WIDTH / 2;
                    int roofY = tile / HEX_GRID_WIDTH / 2;
                    int square = _square[gDude->elevation]->tileFid[roofX + SQUARE_GRID_WIDTH * roofY];
                    if (RoofTileFrmId(square).frameId<TileFrameId>() == TileFrameId::Grid) {
                        continue;
                    }

                    if (_obj_blocking_at(gDude, tile, gDude->elevation) == nullptr) {
                        target = tile;
                        bestDistance = distance;
                    }
                }
            }

            if (target != -1) {
                devAutotestLog("  step from %d to %d (%s a roof)\n", gDude->tile, target, step->a != 0 ? "out from" : "under");
                Rect rect;
                objectSetLocation(gDude, target, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
            } else {
                devAutotestLog("FAIL: no tile under a roof nearby\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_TAP_THEN_STALL: {
            float x;
            float y;
            if (muiGetWidgetCenter(step->widget, &x, &y)) {
                devAutotestPushFinger(SDL_FINGERDOWN, 8, x, y);
                devAutotestPushFinger(SDL_FINGERUP, 8, x, y);
                SDL_Delay(static_cast<Uint32>(step->a));
            } else {
                devAutotestLog("FAIL: mui %s NOT FOUND\n", step->widget);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_MENU_PICTURES: {
            int width;
            int height;
            SDL_Texture* named = muiPictureTexture(InterfaceFrmId("HR_MAINMENU.FRM"), &width, &height);
            bool ok = named != nullptr && width > 0 && height > 0;
            int namedWidth = width;
            int namedHeight = height;
            SDL_Texture* legacy = muiPictureTexture(FrmId(InterfaceFrameId::MainMenuBackgroundImage), &width, &height);
            ok = ok && legacy != nullptr && legacy != named && width > 0 && height > 0;
            for (int attempt = 0; attempt < 2; attempt++) {
                SDL_Texture* missing = muiPictureTexture(InterfaceFrmId("__CE_MISSING_PICTURE__.FRM"), &width, &height);
                ok = ok && missing == nullptr && width == 0 && height == 0;
            }
            SDL_Texture* cached = muiPictureTexture(InterfaceFrmId("HR_MAINMENU.FRM"), &width, &height);
            ok = ok && cached == named && width == namedWidth && height == namedHeight;
            devAutotestLog("  menu pictures: named %dx%d, numeric, missing path, cached identity: %s\n", namedWidth, namedHeight, ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_WIDGET: {
            float x;
            float y;
            bool present = muiGetWidgetCenter(step->widget, &x, &y);
            bool ok = present == (step->a != 0);
            devAutotestLog("  widget %s %s: %s\n", step->widget, present ? "present" : "absent", ok ? "PASS" : "FAIL");
            if (!ok) {
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_DRAG_BY: {
            float x;
            float y;
            if (step->widget != nullptr && muiGetWidgetCenter(step->widget, &x, &y)) {
                float width = static_cast<float>(screenGetWidth());
                float height = static_cast<float>(screenGetHeight());
                devAutotestLog("  mui drag %s by (%.2f, %.2f)\n", step->widget, step->a, step->b);
                devAutotestStartTouch(x * width, y * height, (x + step->a) * width, (y + step->b) * height, 0, 25);
            } else {
                devAutotestLog("FAIL: mui %s NOT FOUND\n", step->widget != nullptr ? step->widget : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_PREFERENCES: {
            PreferenceValues values = preferencesGetValues();
            devAutotestLog("  preferences: combat speed %d, player speedup %d, master volume %d, brightness %.3f, text delay %.1f, violence %d\n",
                values.combatSpeed, values.playerSpeedup, values.masterVolume, values.brightness, values.textBaseDelay, values.violenceLevel);
            devAutotestLog("  settings: all animations %d, walk when sneaking %d, hud scale %d, map filter %d\n",
                settings.enhancements.combat_speed_all_animations ? 1 : 0, settings.qol.walk_when_sneaking ? 1 : 0,
                settings.touch.hud_scale, settings.world_view.filter);
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_SAVES: {
            lsgMobileRefreshSlots();
            int count = 0;
            std::string quick;
            for (int slot = 0; slot < 1000; slot++) {
                MobileSaveSlotInfo info;
                if (lsgMobileGetSlotInfo(slot, &info) && info.state == MobileSaveSlotState::Occupied) {
                    count++;
                    if (lsgMobileIsQuickSlot(slot)) {
                        quick += " " + std::to_string(slot + 1);
                    }
                }
            }
            devAutotestLog("  saves: %d, quick saves setting %d (auto_quick_save %d), quick:%s\n",
                count, lsgQuickSaveCount(), settings.ui.auto_quick_save, quick.empty() ? " none" : quick.c_str());
            if (count != static_cast<int>(step->a)) {
                devAutotestLog("FAIL: %d saves, expected %d\n", count, static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MAP_TRANSITION: {
            char name[COMPAT_MAX_PATH];
            strncpy(name, step->widget, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            MapTransition transition;
            transition.map = wmMapMatchNameToIdx(name);
            transition.elevation = static_cast<int>(step->a);
            transition.tile = static_cast<int>(step->b);
            transition.rotation = ROTATION_NE;
            devAutotestLog("  map %d now, to %s (%d), party %d\n", static_cast<int>(gMapHeader.index), step->widget,
                static_cast<int>(transition.map), static_cast<int>(get_all_party_members_objects(true).size()));
            if (transition.map == MAP_INVALID) {
                devAutotestLog("FAIL: no map %s\n", step->widget);
            } else {
                mapSetTransition(&transition);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_LOG: {
            if (step->a == 0) {
                touchLogStartForTest();
                break;
            }

            if (step->a == 2) {
                touchLogExit();
                break;
            }

            FILE* stream = fopen("touch.log", "r");
            if (stream == nullptr) {
                devAutotestLog("FAIL: no touch.log\n");
                break;
            }

            std::string text;
            char line[512];
            int dumps = 0;
            int fingers = 0;
            while (fgets(line, sizeof(line), stream) != nullptr) {
                text += line;
                if (strncmp(line, "=== ", 4) == 0) {
                    dumps++;
                    devAutotestLog("  touch.log dump:%s", strchr(line + 4, ' ') != nullptr ? strchr(line + 4, ' ') : line);
                } else if (strstr(line, "-> ") != nullptr) {
                    fingers++;
                }
            }
            fclose(stream);

            devAutotestLog("  touch.log: %d dump(s), %d finger events\n", dumps, fingers);
            const char* reasons[] = { step->widget, step->target };
            for (const char* reason : reasons) {
                if (reason != nullptr && text.find(std::string("! ") + reason) == std::string::npos) {
                    devAutotestLog("FAIL: touch.log has no \"%s\"\n", reason);
                }
            }
            if (step->c == 1 && text.find("  game closed\n") == std::string::npos) {
                devAutotestLog("FAIL: touch.log has no \"game closed\" dump\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_FINGER:
            if (step->a == 2) {
                devAutotestPushFinger(SDL_FINGERDOWN, 7, step->b, step->c);
                devAutotestPushFinger(SDL_FINGERUP, 7, step->b, step->c);
            } else {
                devAutotestPushFinger(step->a == 0 ? SDL_FINGERDOWN : SDL_FINGERUP, 7, step->b, step->c);
            }
            break;
        case DEV_AUTOTEST_ACTION_PAN_FRAMES:
            gDevAutotestPanX = step->a;
            gDevAutotestPanY = step->b;
            gDevAutotestPanFrames = static_cast<int>(step->c);
            break;
        case DEV_AUTOTEST_ACTION_PERF_LOG: {
            int frames = gDevAutotestFrames - gDevAutotestPerfFrame;
            unsigned int now = getTicks();
            long long uploads = perfMonitorGetTotal(PerfCount::WorldUploads);
            long long pixels = perfMonitorGetTotal(PerfCount::WorldPixels);
            long long drawn = perfMonitorGetTotal(PerfCount::WorldDrawnPixels);
            if (frames > 0 && gDevAutotestPerfTime != 0) {
                devAutotestLog("  perf %s: %d frames, %.1f ms/frame, per frame: drawn %.0f kpx, uploads %.1f, %.0f kpx\n",
                    step->name,
                    frames,
                    static_cast<double>(getTicksBetween(now, gDevAutotestPerfTime)) / frames,
                    static_cast<double>(drawn - gDevAutotestPerfDrawn) / frames / 1000.0,
                    static_cast<double>(uploads - gDevAutotestPerfUploads) / frames,
                    static_cast<double>(pixels - gDevAutotestPerfPixels) / frames / 1000.0);
            }
            gDevAutotestPerfDrawn = drawn;
            gDevAutotestPerfFrame = gDevAutotestFrames;
            gDevAutotestPerfTime = now;
            gDevAutotestPerfUploads = uploads;
            gDevAutotestPerfPixels = pixels;
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_ANIMATION_SOUND: {
            Sound* sound = animationLoadSoundEffect(gDude, step->widget);
            if (sound != nullptr) {
                int bytesPerSecond = sound->bitsPerSample / 8 * sound->channels * sound->rate;
                devAutotestLog("  sound %s: %d ms (tempo %.2f)\n", step->widget, static_cast<int>(static_cast<long long>(sound->fileSize) * 1000 / bytesPerSecond), sound->tempo);
                soundEffectDelete(sound);
            } else {
                devAutotestLog("FAIL: sound %s not loaded\n", step->widget);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_SET_ACTION_POINTS:
            gDude->data.critter.combat.ap = static_cast<int>(step->a);
            interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
            break;
        case DEV_AUTOTEST_ACTION_ADD_EXPERIENCE:
            pcAddExperience(static_cast<int>(step->a));
            break;
        case DEV_AUTOTEST_ACTION_LOG_CHARACTER: {
            int perks = 0;
            for (int perk = 0; perk < PERK_COUNT; perk++) {
                perks += perkGetRank(gDude, static_cast<Perk>(perk));
            }
            devAutotestLog("  character: level %d, xp %d, skill points %d, perks owed %d, perk ranks %d, small guns %d, big guns %d, unarmed %d\n",
                pcGetStat(PC_STAT_LEVEL),
                pcGetStat(PC_STAT_EXPERIENCE),
                pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS),
                characterEditorGetPerkOwed(),
                perks,
                skillGetValue(gDude, SKILL_SMALL_GUNS),
                skillGetValue(gDude, SKILL_BIG_GUNS),
                skillGetValue(gDude, SKILL_UNARMED));
            break;
        }
        case DEV_AUTOTEST_ACTION_PLAY_MOVIE:
            devAutotestLog("  playing movie %d\n", static_cast<int>(step->a));
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            gameMoviePlay(static_cast<int>(step->a), GAME_MOVIE_FADE_IN | GAME_MOVIE_FADE_OUT | GAME_MOVIE_PAUSE_MUSIC);
            devAutotestLog("  movie closed\n");
            return;
        case DEV_AUTOTEST_ACTION_CHECK_MOVIE: {
            int width = 0;
            int height = 0;
            bool playing = movieIsMobile() && movieGetFrameTexture(&width, &height) != nullptr;
            devAutotestLog("  movie frame: active %d, %dx%d\n", playing ? 1 : 0, width, height);
            if (!playing) {
                devAutotestLog("autotest failed: expected a decoded movie frame\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_POINTER: {
            int x;
            int y;
            mouseGetPointerPosition(&x, &y);
            int worldX;
            int worldY;
            worldViewScreenToWorld(x, y, &worldX, &worldY);
            int tile = tileFromScreenXY(worldX, worldY);
            Object* object = playerObjectAt(worldX, worldY, OBJ_TYPE_INVALID, true, gElevation);
            Object* critter = devAutotestFindNearestCritter();
            devAutotestLog("  pointer (%d, %d), tile %d, object pid %d%s, buttons %d, click hook calls %d\n",
                x, y, tile,
                object != nullptr ? object->pid : -1,
                object != nullptr && object == critter ? " (the critter)" : "",
                mouseGetPointerButtons(),
                inputGetMouseClickHookCalls());
            break;
        }
        case DEV_AUTOTEST_ACTION_WORLDMAP_AREA: {
            City area = static_cast<City>(static_cast<int>(step->a));
            wmAreaSetVisibleState(area, CITY_STATE_KNOWN, true);
            // An unknown area becomes known first (the game's rule).
            wmAreaMarkVisitedState(area, VisitedState::Visited);
            wmAreaMarkVisitedState(area, VisitedState::Visited);
            wmTeleportToArea(area);
            devAutotestLog("  party at area %d\n", static_cast<int>(area));
            break;
        }
        case DEV_AUTOTEST_ACTION_KILL_DUDE:
            critterKill(gDude, ANIM_INVALID, true);
            devAutotestLog("  dude killed: results 0x%x, hp %d\n", gDude->data.critter.combat.results, gDude->data.critter.hp);
            break;
        case DEV_AUTOTEST_ACTION_REQUEST_ENDGAME:
            scriptsRequestEndgame();
            break;
        case DEV_AUTOTEST_ACTION_OPEN_ELEVATOR: {
            Map map = static_cast<Map>(mapGetCurrentMap());
            int elevation = gElevation;
            int tile = -1;
            devAutotestLog("  opening elevator %d at map %d elevation %d\n", static_cast<int>(step->a), map, elevation);
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            int rc = elevatorSelectLevel(static_cast<int>(step->a), &map, &elevation, &tile);
            devAutotestLog("  elevator closed: rc %d, map %d elevation %d tile %d\n", rc, map, elevation, tile);
            return;
        }
        case DEV_AUTOTEST_ACTION_OPEN_CHARACTER_CREATION:
            devAutotestLog("  opening character creation\n");
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            characterEditorShow(true);
            devAutotestLog("  character creation closed\n");
            return;
        case DEV_AUTOTEST_ACTION_TAP_DIALOG_WINDOW: {
            Rect rect;
            int win = gameDialogGetWindow();
            if (win != -1 && windowGetRect(win, &rect) == 0) {
                float x = static_cast<float>(rect.left + step->a);
                float y = static_cast<float>(rect.top + step->b);
                devAutotestLog("  dialog window at (%d, %d), tap (%.0f, %.0f)\n", rect.left, rect.top, x, y);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            } else {
                devAutotestLog("  no dialog window\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_PAN_DURING_ENEMY_TURN: {
            Rect visibleRect;
            worldViewGetVisibleRect(&visibleRect);
            devAutotestLog("  before: visible world rect (%d, %d) - (%d, %d)\n", visibleRect.left, visibleRect.top, visibleRect.right, visibleRect.bottom);
            gameCommandPost(GameCommandType::EndTurn);
            float x = static_cast<float>(screenGetWidth() / 2);
            float y = static_cast<float>(screenGetVisibleHeight() / 2);
            devAutotestStartTouch(x, y, x + step->a, y + step->b, 0, 15);
            gDevAutotestTouchDelay = static_cast<int>(step->c);
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_DRAG: {
            float x = static_cast<float>(screenGetWidth() / 2);
            float y = static_cast<float>(screenGetVisibleHeight() / 2);
            devAutotestStartTouch(x, y, x + step->a, y + step->b, 0, 15);
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_TILE:
            if (gDude != nullptr) {
                gDevAutotestTouchStartTile = gDude->tile;
                gDevAutotestWalkTarget = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(static_cast<int>(step->b)), static_cast<int>(step->a));
                float x;
                float y;
                devAutotestGetTileScreenPosition(gDevAutotestWalkTarget, &x, &y);
                devAutotestLog("  tap from %d to %d\n", gDude->tile, gDevAutotestWalkTarget);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        case DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER:
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                float x;
                float y;
                devAutotestGetObjectScreenPosition(critter, &x, &y);
                bool longPress = step->action == DEV_AUTOTEST_ACTION_TOUCH_LONG_PRESS_CRITTER;
                devAutotestLog("  %s critter at tile %d (distance %d)\n", longPress ? "long press" : "tap", critter->tile, objectGetDistanceBetween(gDude, critter));
                devAutotestStartTouch(x, y, x, y, longPress ? 45 : 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                int tile = tileGetTileInDirection(critter->tile, tileGetRotationTo(critter->tile, gDude->tile), 1);
                float x;
                float y;
                devAutotestGetTileScreenPosition(tile, &x, &y);
                devAutotestLog("  tap tile %d next to critter at %d\n", tile, critter->tile);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_GIVE_ITEM: {
            // `b` - quantity.
            Object* item;
            if (objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                int quantity = step->b > 0 ? static_cast<int>(step->b) : 1;
                // As `add_obj_to_inven`: off the map once carried (or it is
                // freed with the map and the inventory both).
                if (itemAdd(gDude, item, quantity) == 0) {
                    _obj_disconnect(item, nullptr);
                }
                devAutotestLog("  gave item pid %d x%d\n", static_cast<int>(step->a), quantity);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_KEY: {
            SDL_Event event;
            SDL_zero(event);
            event.type = SDL_KEYDOWN;
            event.key.state = SDL_PRESSED;
            event.key.keysym.scancode = static_cast<SDL_Scancode>(static_cast<int>(step->a));
            SDL_PushEvent(&event);
            event.type = SDL_KEYUP;
            event.key.state = SDL_RELEASED;
            SDL_PushEvent(&event);
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_SCREEN:
            devAutotestStartTouch(step->a, step->b, step->a, step->b, 1, 0);
            break;
        case DEV_AUTOTEST_ACTION_LOG_INVENTORY: {
            Object* rightHand = critterGetItem2(gDude);
            Object* leftHand = critterGetItem1(gDude);
            Object* armor = critterGetArmor(gDude);
            devAutotestLog("  inventory length %d, right hand pid %d, left hand pid %d, armor pid %d\n",
                gDude->data.inventory.length,
                rightHand != nullptr ? rightHand->pid : -1,
                leftHand != nullptr ? leftHand->pid : -1,
                armor != nullptr ? armor->pid : -1);

            // Items: pid x quantity, loaded ammo, container contents.
            Inventory* inventory = &(gDude->data.inventory);
            for (int index = 0; index < inventory->length; index++) {
                Object* item = inventory->items[index].item;
                devAutotestLog("    pid %d x%d", item->pid, inventory->items[index].quantity);
                if (itemGetType(item) == ITEM_TYPE_WEAPON && ammoGetCapacity(item) > 0) {
                    devAutotestLog(" ammo %d/%d", ammoGetQuantity(item), ammoGetCapacity(item));
                }
                if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
                    devAutotestLog(" contains %d", item->data.inventory.length);
                }
                devAutotestLog("\n");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_ADD_PARTY_MEMBER: {
            Object* critter;
            int pid = 0x1000000 | static_cast<int>(step->a);
            if (objectCreateWithProtoId(&critter, ProtoId(pid)) == 0) {
                int distance = step->b > 0 ? static_cast<int>(step->b) : 2;
                Rotation direction = step->b > 0 ? static_cast<Rotation>(static_cast<int>(step->c)) : ROTATION_SE;
                int tile = tileGetTileInDirection(gDude->tile, direction, distance);
                Rect rect;
                objectSetLocation(critter, tile, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
                devAutotestAttachScript(critter, step->widget);
                int rc = partyMemberAdd(critter);
                gDevAutotestPartyMember = critter;
                devAutotestLog("  party member pid %d at tile %d: %s\n", pid, tile, rc == 0 ? "joined" : "FAIL");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_FILL_PARTY_WEIGHT: {
            Object* member = gDevAutotestPartyMember;
            int pid = static_cast<int>(step->a);
            Object* item;
            if (member != nullptr && objectCreateWithProtoId(&item, ProtoId(pid)) == 0) {
                int unit = std::max(itemGetWeight(item), 1);
                int free = critterGetStat(member, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(member);
                int quantity = (free - static_cast<int>(step->b)) / unit;
                if (quantity > 0 && itemAdd(member, item, quantity) == 0) {
                    _obj_disconnect(item, nullptr);
                } else {
                    objectDestroy(item, nullptr);
                }
                devAutotestLog("  party member filled with pid %d x%d, can carry %d more\n", pid, std::max(quantity, 0),
                    critterGetStat(member, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(member));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MOVE_PARTY_MEMBER:
            if (gDevAutotestPartyMember != nullptr) {
                int tile = tileGetTileInDirection(gDude->tile, ROTATION_SE, static_cast<int>(step->a));
                Rect rect;
                objectSetLocation(gDevAutotestPartyMember, tile, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
                devAutotestLog("  party member at tile %d, %d tiles away\n", tile, tileDistanceBetween(gDude->tile, tile));
            }
            break;
        case DEV_AUTOTEST_ACTION_GIVE_PARTY_ITEM: {
            Object* item;
            if (gDevAutotestPartyMember != nullptr && objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                int quantity = step->b > 0 ? static_cast<int>(step->b) : 1;
                if (itemAdd(gDevAutotestPartyMember, item, quantity) == 0) {
                    _obj_disconnect(item, nullptr);
                }
                devAutotestLog("  gave party member item pid %d x%d\n", static_cast<int>(step->a), quantity);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_WIELD_PARTY_ITEM:
            if (gDevAutotestPartyMember != nullptr) {
                Object* item = objectGetCarriedObjectByProtoId(gDevAutotestPartyMember, ProtoId(static_cast<int>(step->a)));
                int rc = item != nullptr ? inventoryEquip(gDevAutotestPartyMember, item, HAND_RIGHT) : -1;
                devAutotestLog("  party member wields pid %d: %s\n", static_cast<int>(step->a), rc != -1 ? "ok" : "FAIL");
            }
            break;
        case DEV_AUTOTEST_ACTION_SET_NAME:
            if (step->widget != nullptr) {
                dudeSetName(step->widget);
            }
            break;
        case DEV_AUTOTEST_ACTION_LOAD_GAME: {
            int rc = lsgLoadGameFromSlot(static_cast<int>(step->a));
            devAutotestLog("  loaded slot %d: %s\n", static_cast<int>(step->a) + 1, rc != -1 ? "ok" : "FAIL");
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_PARTY:
            for (Object* member : get_all_party_members_objects(false)) {
                Object* weapon = critterGetItem2(member);
                devAutotestLog("  party member pid %d, items %d, script %d, in hand pid %d\n", member->pid, member->data.inventory.length, member->sid, weapon != nullptr ? weapon->pid : -1);
            }
            break;
        case DEV_AUTOTEST_ACTION_QUICK_SAVE:
            if (lsgSaveGame(LOAD_SAVE_MODE_QUICK) != 1) {
                devAutotestLog("FAIL: quick save\n"); exit(EXIT_FAILURE);
            }
            break;
        case DEV_AUTOTEST_ACTION_QUICK_LOAD:
            if (lsgLoadGame(LOAD_SAVE_MODE_QUICK) != 1) {
                devAutotestLog("FAIL: quick load\n"); exit(EXIT_FAILURE);
            }
            break;
        case DEV_AUTOTEST_ACTION_CHECK_SAVE_SESSION: {
            int actual = lsgSessionLoadSlot();
            const char* name = critterGetName(gDude);
            bool ok = actual == static_cast<int>(step->a)
                && (step->widget == nullptr || strcmp(name, step->widget) == 0);
            devAutotestLog("  session target=%d expected=%d name=%s: %s\n", actual, static_cast<int>(step->a), name, ok ? "PASS" : "FAIL");
            if (!ok) exit(EXIT_FAILURE);
            break;
        }
        case DEV_AUTOTEST_ACTION_SAVE_GAME: {
            int rc = lsgSaveGameToSlot(static_cast<int>(step->a), step->widget != nullptr ? step->widget : "TEST");
            devAutotestLog("  saved to slot %d: %s\n", static_cast<int>(step->a) + 1, rc != -1 ? "ok" : "FAIL");
            break;
        }
        case DEV_AUTOTEST_ACTION_FLOAT_TEXT: {
            // Centered on dude, so the text is on screen. [a] 1 - the color
            // of scripts' `float_msg` and [widget]'s text.
            tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            char text[256] = "Floating text test";
            if (step->widget != nullptr) {
                snprintf(text, sizeof(text), "%s", step->widget);
            }
            Rect rect;
            int rc = textObjectAdd(gDude, text, 101, step->a == 1 ? COLOR_LIGHT_YELLOW : COLOR_GREEN, COLOR_BLACK, &rect);
            if (rc == 0) {
                tileWindowRefreshRect(&rect, gElevation);
            }
            devAutotestLog("  floating text over dude: rc %d, rect (%d, %d) - (%d, %d)\n", rc, rect.left, rect.top, rect.right, rect.bottom);
            break;
        }
        case DEV_AUTOTEST_ACTION_KILL_CRITTER:
            gDevAutotestCorpse = devAutotestFindNearestCritter();
            if (gDevAutotestCorpse != nullptr) {
                critterKill(gDevAutotestCorpse, ANIM_INVALID, true);
                devAutotestLog("  killed critter at tile %d\n", gDevAutotestCorpse->tile);
            }
            break;
        case DEV_AUTOTEST_ACTION_GIVE_CORPSE_ITEM: {
            Object* item;
            if (gDevAutotestCorpse != nullptr && objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                itemAdd(gDevAutotestCorpse, item, 1);
                devAutotestLog("  gave corpse item pid %d\n", static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_CENTER_CORPSE:
            if (gDevAutotestCorpse != nullptr) {
                tileSetCenter(gDevAutotestCorpse->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            }
            break;
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_CORPSE:
            if (gDevAutotestCorpse != nullptr) {
                float x;
                float y;
                devAutotestGetObjectScreenPosition(gDevAutotestCorpse, &x, &y);
                devAutotestLog("  tap corpse at tile %d\n", gDevAutotestCorpse->tile);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        case DEV_AUTOTEST_ACTION_CENTER_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                tileSetCenter(critter->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_AWAY_FROM_CRITTER: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                Rotation rotation = static_cast<Rotation>((tileGetRotationTo(gDude->tile, critter->tile) + 3) % ROTATION_COUNT);
                gDevAutotestWalkTarget = tileGetTileInDirection(gDude->tile, rotation, 1);
                float x;
                float y;
                devAutotestGetTileScreenPosition(gDevAutotestWalkTarget, &x, &y);
                devAutotestLog("  tap tile %d away from critter\n", gDevAutotestWalkTarget);
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_OPEN_WORLDMAP:
            // World map loop runs inside, the script continues from there.
            gDevAutotestPendingCapture = step->name;
            gDevAutotestStep++;
            gDevAutotestStepStarted = false;
            wmWorldMap();
            return;
        case DEV_AUTOTEST_ACTION_LOG_WORLDMAP: {
            int offsetX;
            int offsetY;
            int partyX;
            int partyY;
            bool isWalking;
            Rect viewRect;
            wmGetInterfaceState(&offsetX, &offsetY, &partyX, &partyY, &isWalking, &viewRect);
            devAutotestLog("  world map offset (%d, %d), party (%d, %d), walking %d\n", offsetX, offsetY, partyX, partyY, isWalking ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_WORLDMAP_DRAG:
        case DEV_AUTOTEST_ACTION_WORLDMAP_TAP: {
            int offsetX;
            int offsetY;
            int partyX;
            int partyY;
            bool isWalking;
            Rect viewRect;
            wmGetInterfaceState(&offsetX, &offsetY, &partyX, &partyY, &isWalking, &viewRect);
            if (step->action == DEV_AUTOTEST_ACTION_WORLDMAP_DRAG) {
                float x = static_cast<float>((viewRect.left + viewRect.right) / 2);
                float y = static_cast<float>((viewRect.top + viewRect.bottom) / 2);
                devAutotestStartTouch(x, y, x + step->a, y + step->b, 0, 15);
            } else {
                float x = static_cast<float>(viewRect.left + partyX - offsetX) + step->a;
                float y = static_cast<float>(viewRect.top + partyY - offsetY) + step->b;
                devAutotestStartTouch(x, y, x, y, 1, 0);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_EQUIP_ITEM: {
            Object* item;
            if (objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                itemAdd(gDude, item, 1);
                int rc = inventoryEquipFunc(gDude, item, static_cast<Hand>(static_cast<int>(step->b)), false);
                interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
                devAutotestLog("  equipped pid %d in hand %d: rc %d\n", static_cast<int>(step->a), static_cast<int>(step->b), rc);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_MARKER: {
            int tile = mapHintsGetDestination();
            if (tile == -1) {
                devAutotestLog("  no destination marker\n");
                break;
            }
            float x;
            float y;
            devAutotestGetTileScreenPosition(tile, &x, &y);
            devAutotestLog("  tap near marker tile %d\n", tile);
            devAutotestStartTouch(x + step->a, y + step->b, x + step->a, y + step->b, 1, 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_MOVEMENT:
            devAutotestLog("  dude tile %d, marker tile %d, animation %d (walk %d, run %d)\n",
                gDude->tile,
                mapHintsGetDestination(),
                FrmId(gDude->fid).animationType(),
                ANIM_WALK,
                ANIM_RUNNING);
            break;
        case DEV_AUTOTEST_ACTION_PLACE_ITEM: {
            Object* item;
            if (objectCreateWithProtoId(&item, ProtoId(static_cast<int>(step->a))) == 0) {
                int tile = tileGetTileInDirection(gDude->tile, static_cast<Rotation>(static_cast<int>(step->c)), static_cast<int>(step->b));
                Rect rect;
                objectSetLocation(item, tile, gDude->elevation, &rect);
                tileWindowRefreshRect(&rect, gDude->elevation);
                gDevAutotestPlacedItem = item;
                devAutotestLog("  placed pid %d at tile %d\n", static_cast<int>(step->a), tile);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_NEAR_ITEM:
            if (gDevAutotestPlacedItem != nullptr) {
                Rect worldRect;
                objectGetRect(gDevAutotestPlacedItem, &worldRect);
                // c = 1 - from the box center, otherwise from bottom right.
                int x;
                int y;
                if (step->c == 1) {
                    worldViewWorldToScreen((worldRect.left + worldRect.right) / 2, (worldRect.top + worldRect.bottom) / 2, &x, &y);
                } else {
                    worldViewWorldToScreen(worldRect.right, worldRect.bottom, &x, &y);
                }
                devAutotestLog("  tap near item at (%d, %d) + (%.0f, %.0f)\n", x, y, step->a, step->b);
                devAutotestStartTouch(x + step->a, y + step->b, x + step->a, y + step->b, 1, 0);
            }
            break;
        case DEV_AUTOTEST_ACTION_MUI_TAP: {
            float x;
            float y;
            if (step->widget != nullptr && muiGetWidgetCenter(step->widget, &x, &y)) {
                x *= screenGetWidth();
                y *= screenGetHeight();
                devAutotestLog("  mui %s at (%.0f, %.0f)\n", step->widget, x, y);
                devAutotestStartTouch(x, y, x, y, step->a == 1 ? -1 : 1, 0);
            } else {
                devAutotestLog("  mui %s: NOT FOUND\n", step->widget != nullptr ? step->widget : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_LONG_PRESS: {
            float x;
            float y;
            if (step->widget != nullptr && muiGetWidgetCenter(step->widget, &x, &y)) {
                static std::string heldName;
                heldName = std::string(step->name) + "_held";
                int hold = static_cast<int>(step->a);
                x *= screenGetWidth();
                y *= screenGetHeight();
                devAutotestLog("  mui long press %s at (%.0f, %.0f)\n", step->widget, x, y);
                devAutotestStartTouch(x, y, x, y, hold, 0, hold - 2, heldName.c_str());
            } else {
                devAutotestLog("  mui %s: NOT FOUND\n", step->widget != nullptr ? step->widget : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_MUI_DRAG: {
            float x;
            float y;
            float targetX;
            float targetY;
            if (step->widget != nullptr && step->target != nullptr
                && muiGetWidgetCenter(step->widget, &x, &y) && muiGetWidgetCenter(step->target, &targetX, &targetY)) {
                float width = static_cast<float>(screenGetWidth());
                float height = static_cast<float>(screenGetHeight());
                devAutotestLog("  mui drag %s -> %s\n", step->widget, step->target);
                devAutotestStartTouch(x * width, y * height, targetX * width, targetY * height, 0, 25);
            } else {
                devAutotestLog("  mui drag %s -> %s: NOT FOUND\n", step->widget != nullptr ? step->widget : "?", step->target != nullptr ? step->target : "?");
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_PLACED_ITEM:
            if (gDevAutotestPlacedItem != nullptr) {
                // Picked item may be merged into a stack and freed, look
                // for it on the map instead of reading it.
                bool onGround = false;
                for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
                    if (object == gDevAutotestPlacedItem) {
                        onGround = true;
                    }
                }
                devAutotestLog("  placed item %s\n", onGround ? "on the ground" : "PICKED UP");
            }
            break;
        case DEV_AUTOTEST_ACTION_LOG_SLOT:
        {
            char path[64];
            snprintf(path, sizeof(path), "SAVEGAME\\SLOT%.2d\\SAVE.DAT", static_cast<int>(step->a));
            int size;
            devAutotestLog("  slot %d has save: %d\n", static_cast<int>(step->a), dbGetFileSize(path, &size) == 0 ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_HUD_LONG_PRESS: {
            int x;
            int y;
            if (touchHudGetElementScreenCenter(static_cast<HudElementId>(static_cast<int>(step->a)), &x, &y)) {
                static std::string heldName;
                heldName = std::string(step->name) + "_held";
                int hold = static_cast<int>(step->b);
                devAutotestLog("  hud long press %d at (%d, %d)\n", static_cast<int>(step->a), x, y);
                devAutotestStartTouch(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y), hold, 0, hold - 2, heldName.c_str());
            } else {
                devAutotestLog("  hud element %d: NOT FOUND\n", static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_HUD_TAP:
        case DEV_AUTOTEST_ACTION_HUD_TAP_MODE:
        case DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE: {
            int x;
            int y;
            bool found = step->action == DEV_AUTOTEST_ACTION_HUD_TAP
                ? touchHudGetElementScreenCenter(static_cast<HudElementId>(static_cast<int>(step->a)), &x, &y)
                : touchHudGetModeScreenCenter(static_cast<int>(step->a), &x, &y);
            if (found && step->action == DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE) {
                // Last chip, right part (where the finger lands at the edge).
                x += static_cast<int>(step->b);
            }
            if (found) {
                devAutotestLog("  hud %s %d at (%d, %d)\n", step->action == DEV_AUTOTEST_ACTION_HUD_TAP ? "element" : "mode", static_cast<int>(step->a), x, y);
                int hold = step->action == DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE ? -1 : 1;
                devAutotestStartTouch(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y), hold, 0);
            } else {
                devAutotestLog("  hud %d: NOT FOUND\n", static_cast<int>(step->a));
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_HIT_MODE: {
            HitMode hitMode;
            bool aiming;
            interfaceGetCurrentHitMode(&hitMode, &aiming);
            devAutotestLog("  hit mode %d, aiming %d\n", hitMode, aiming ? 1 : 0);
            break;
        }
        case DEV_AUTOTEST_ACTION_CHECK_MODS:
            devAutotestCheckMods();
            break;
        case DEV_AUTOTEST_ACTION_CHECK_LOADED_WORLD: {
            int lit = 0;
            if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
                const SDL_Color* colors = gSdlSurface->format->palette->colors;
                for (int index = 1; index < 229; index++) {
                    if (colors[index].r != 0 || colors[index].g != 0 || colors[index].b != 0) lit++;
                }
            }
            devAutotestLog("  non-cycling palette entries with color: %d\n", lit);
            devAutotestLog("  touch state: enabled %d, map input %d, map scroll %d, cursor %d, mode %d, iso disabled %d, ui disabled %d, touchscreen %d, world hit %d\n",
                touchControlsIsEnabled() ? 1 : 0,
                gameMouseIsMapInputEnabled() ? 1 : 0,
                gameMouseIsMapScrollingEnabled() ? 1 : 0,
                gameMouseGetCursor(),
                gameMouseGetMode(),
                isoIsDisabled() ? 1 : 0,
                gameUiIsDisabled() ? 1 : 0,
                touch_get_touchscreen_mode() ? 1 : 0,
                worldViewIsWorldAt(screenGetWidth() / 2, screenGetVisibleHeight() / 2) ? 1 : 0);
            if (!worldViewIsEnabled() || gDude == nullptr || gDude->tile < 0 || lit < 16
                || !gameMouseIsMapInputEnabled()
                || !worldViewIsWorldAt(screenGetWidth() / 2, screenGetVisibleHeight() / 2)) {
                devAutotestLog("autotest failed: loaded world palette or map touch input is unavailable\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_LOG_COMBAT:
            devAutotestLog("  in combat %d, dude ap %d, dude tile %d, game mouse mode %d\n", isInCombat() ? 1 : 0, gDude->data.critter.combat.ap, gDude->tile, gameMouseGetMode());
            break;
        case DEV_AUTOTEST_ACTION_START_COMBAT: {
            Object* critter = devAutotestFindNearestCritter();
            if (critter != nullptr) {
                float x;
                float y;
                devAutotestGetObjectScreenPosition(critter, &x, &y);
                devAutotestLog("  starting combat, nearest critter at tile %d\n", critter->tile);

                // Combat loop runs inside, the script continues from there,
                // so this step is completed first.
                gDevAutotestPendingCapture = step->name;
                gDevAutotestStep++;
                gDevAutotestStepStarted = false;

                // Same as "A" key.
                _combat(nullptr);
                return;
            }
            break;
        }
        case DEV_AUTOTEST_ACTION_TOUCH_TAP_RADIAL: {
            int x;
            int y;
            if (touchControlsGetRadialMenuItemPosition(static_cast<int>(step->a), &x, &y)) {
                devAutotestLog("  radial item %d at (%d, %d)\n", static_cast<int>(step->a), x, y);
                devAutotestStartTouch(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x), static_cast<float>(y), 1, 0);
            } else {
                devAutotestLog("  radial item %d: MENU NOT OPEN\n", static_cast<int>(step->a));
            }
            break;
        }
        default:
            break;
        }

        worldViewGetVisibleRect(&visibleRect);
        devAutotestLog("%s: zoom %.3f, center tile %d, visible world rect (%d, %d) - (%d, %d)\n",
            step->name,
            worldViewGetZoom(),
            gCenterTile,
            visibleRect.left,
            visibleRect.top,
            visibleRect.right,
            visibleRect.bottom);
        devAutotestCheckDudeRoundTrip();
        return;
    }

    // Touch is advanced by `devAutotestPump`, wait for it.
    if (gDevAutotestTouchFrame != -1) {
        return;
    }

    if (gDevAutotestDelay > 0) {
        gDevAutotestDelay--;
        return;
    }

    gDevAutotestPendingCapture = step->name;
    gDevAutotestStep++;
    gDevAutotestStepStarted = false;
    gDevAutotestPinchFrame = -1;
}

void devAutotestPump()
{
    if (!gDevAutotestEnabled || gDevAutotestTouchFrame == -1) {
        return;
    }

    if (devAutotestTouchTick()) {
        Rect visibleRect;
        worldViewGetVisibleRect(&visibleRect);
        devAutotestLog("  after touch: visible world rect (%d, %d) - (%d, %d)%s\n", visibleRect.left, visibleRect.top, visibleRect.right, visibleRect.bottom,
            isInCombat() && _combat_whose_turn() != gDude ? ", others' turn" : "");
    }
}

void devAutotestCapture(SDL_Renderer* renderer)
{
    if (gDevAutotestPendingCapture == nullptr) {
        return;
    }

    const char* name = gDevAutotestPendingCapture;
    gDevAutotestPendingCapture = nullptr;

    if (gDevAutotestTraceItems) {
        devAutotestLog("%s: state\n", name);
        devAutotestLogItems();
        devAutotestLogInterface();
    }

    int width;
    int height;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        return;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ABGR8888, pixels.data(), width * 4) != 0) {
        devAutotestLog("%s: read pixels failed: %s\n", name, SDL_GetError());
        return;
    }

    std::string path = gDevAutotestOutputPath + "/" + name + ".png";
    unsigned error = lodepng::encode(path, pixels, width, height, LCT_RGBA, 8);
    devAutotestLog("%s: saved %dx%d frame (%s)\n", name, width, height, error == 0 ? "ok" : "error");

    // The game's windows the screen shows besides the map: under the mobile
    // UI screens there should be none of the game's own.
    std::string windows;
    for (int id = 1; id < 50; id++) {
        Window* window = windowGetWindow(id);
        if (window == nullptr || (window->flags & WINDOW_HIDDEN) != 0 || id == gIsoWindow) {
            continue;
        }
        char entry[64];
        snprintf(entry, sizeof(entry), " %d(%dx%d at %d,%d)", id, window->width, window->height, window->rect.left, window->rect.top);
        windows += entry;
    }
    if (!windows.empty()) {
        devAutotestLog("  visible game windows:%s\n", windows.c_str());
    }
}

void devAutotestCaptureNextFrame(const char* name)
{
    if (devAutotestIsEnabled()) {
        gDevAutotestPendingCapture = name;
    }
}

// Checks that dude's tile survives screen -> world -> tile conversion, which
// is what mouse picking relies on.
static void devAutotestCheckDudeRoundTrip()
{
    if (gDude == nullptr) {
        return;
    }

    int worldX;
    int worldY;
    tileToScreenXY(gDude->tile, &worldX, &worldY);

    int screenX;
    int screenY;
    worldViewWorldToScreen(worldX + 16, worldY + 8, &screenX, &screenY);

    int pickedX;
    int pickedY;
    worldViewScreenToWorld(screenX, screenY, &pickedX, &pickedY);

    int pickedTile = tileFromScreenXY(pickedX, pickedY);
    devAutotestLog("  dude tile %d at screen (%d, %d), picked tile %d: %s\n",
        gDude->tile,
        screenX,
        screenY,
        pickedTile,
        pickedTile == gDude->tile ? "ok" : "MISMATCH");
}

static void devAutotestLog(const char* format, ...)
{
    std::string path = gDevAutotestOutputPath + "/log.txt";
    FILE* stream = fopen(path.c_str(), "a");
    if (stream == nullptr) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);

    fclose(stream);
}

void devAutotestNote(const char* format, ...)
{
    if (!devAutotestIsEnabled()) {
        return;
    }
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    devAutotestLog("%s", text);
}

} // namespace fallout
