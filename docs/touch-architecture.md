# Touch-native controls — architecture plan

Goal: a full touch port without a cursor layer. The game rules, scripts and
mods stay the same; only the way player commands reach them changes.

## Findings (2026-09-26)

- Game rules (damage, AP, scripts, movement) take objects and tiles, not
  screen points. The screen is read only by the mouse front end:
  `_gmouse_handle_event` (game_mouse.cc), `_check_move` (animation.cc, dude
  move in combat), the action menu, and the legacy window-manager screens.
- Installed mods: RPU (1537 scripts, sources checked) uses no mouse or
  cursor functions; Party Orders works through hotkeys; sfall item
  highlighting reads `outlined_object`.
- sfall exposes the pointer to scripts: `get_mouse_x/y`,
  `get_mouse_buttons`, `tile_under_cursor`, `obj_under_cursor`,
  `get/set_cursor_mode`, `HOOK_MOUSECLICK`. Only third-party mods use them;
  support stays through a bridge (below).

## Layers

1. **Player commands** (`player_commands.{h,cc}`): every action of the player
   character as a function with an explicit target - walk/run to a tile,
   attack a target (hit mode, called location), use an object, pick up,
   talk, look, use a skill / an item on a target, rotate, push, the list of
   actions available for an object (radial menu). They call the game's own
   functions (the same the mouse code calls), so rules don't change. The
   desktop mouse code is moved onto the same commands: one execution path.
2. **Touch intents**: a targeting controller over the map with states - idle,
   enemy selected, tile selected, waiting for a target (skill, item, throw),
   called shot. It keeps objects and tiles, not screen points; handles
   confirm and cancel.
3. **Presentation**: hints anchored to objects/tiles, fixed size, drawn by
   the mobile UI over the map (`map_hints.cc`): hit chance above the selected
   target, AP cost next to the selected tile, the tile dude walks to; a skill
   waiting for a target is shown only by the HUD hint at the top. Later:
   floating texts. The game's hex cursor and destination marker objects are
   not used with touch controls. AP left and attack cost stay in the HUD
   (weapon block), as on the PC interface bar.

## Mod bridge (no visible cursor)

- "Last map touch point" instead of a cursor: `get_mouse_x/y`,
  `tile_under_cursor`, `obj_under_cursor` return it; a tap / long press
  fires `HOOK_MOUSECLICK` as button 1 / 2 at that point.
- Cursor mode (`get/set_cursor_mode`) maps to the targeting controller
  state (a mod switching to crosshair puts it into "waiting for a target").
- `outlined_object` = the current target.
- Autotest: a test script reads these after a touch.

## Legacy screens

The in-game menu and save/load picker, like inventory, character and Pip-Boy,
now use the mobile UI when enabled. Preferences, the title menu and other
remaining windows still use window-manager buttons and mouse emulation. That
path stays isolated to unported windows and the desktop fallback. When the
last screen is ported, mouse emulation can be removed together with the
tap -> cursor -> click code (`touch.cc`, `mouse.cc`).

The title menu's Load action opens the new picker even though the title menu
itself is still a legacy window. The picker uses native touch widgets and
GPU textures, including for legacy indexed save thumbnails. Its 2026-09-28
follow-up was completed by Codex / GPT-6 Sol after the user's Claude Code /
Claude Opus 5.5 work; see the
[save/load handoff](save-load-handoff-2026-09-28.md) for the exact changes,
verification, and remaining device check.

The subsequent **GPT-6 Astra (`gpt-6-astra`)** follow-up (attribution clarified
by the user on 2026-09-29) adds a shared `SaveCatalog` above the existing CE
save/load routines. Native touch widgets and quick commands use the same
placement/order/session policy; `save_storage` implements complete-directory
transactions. The selected UI row and legacy engine cursor no longer determine
quick retention. This preserves the native touch/GPU UI and CE save format.
See the save-history section of the handoff for implementation and tests.

## Why the map must leave the mouse path (2026-09-29)

Map gestures still go touch.cc (recognizer) -> `_mouse_info` (mouse.cc) ->
`touchControlsHandleGesture`. So the mouse's state decides about the map
although there's no cursor: `_mouse_info` returns while the cursor is
hidden or the mouse disabled, gestures piled up and ran late (now dropped
there, a stopgap), and every stuck-finger fix had to reset the emulated
mouse too. Step 4 removes the cause:

- Touch controls read the recognizer's gestures themselves every frame,
  independent of the cursor; gestures older than the mobile UI's
  `kStaleTouchMs` (a stall) are dropped whole, as the UI's already are.
- With every window a mobile screen, the tap -> cursor -> click emulation
  (touch.cc cursor warp, the touch part of mouse.cc: tap delay, drag
  button, wheel from pans) is deleted. The mouse code stays for real mice
  (desktop); on Android there's no mouse path at all.
- Step 3 first: the mod bridge (`get_mouse_x/y`, `tile_under_cursor`,
  `obj_under_cursor`, `HOOK_MOUSECLICK` from the last touch point), so mods
  reading the mouse keep working when the emulation is gone.

## Status

- Steps 1-2 done (2026-09-26): `player_commands.{h,cc}` (object picking,
  primary action, move to a tile - `dudeMoveToTile` / `dudeRunToTile` in
  animation.cc, attack, active item / skill on a target, look, hit chance,
  move cost); `_gmouse_handle_event` and `gameMouseRefresh` use them; touch
  controls call them with no cursor; `map_hints.cc` draws the hints; the
  CE destination marker object is gone. Autotest logs (default, magnet, hud
  scenarios) are identical to the cursor-based version.
- Steps 3-4 done (2026-09-29), every screen being a mobile one:
  - touch-native input (`touchControlsIsNative`: the mobile UI and touch
    controls on): the map takes the recognizer's gestures itself
    (`touchControlsProcessGestures` from input's background processing, every
    frame, whatever the mouse's state); a tap or a long press older than
    `kTouchStaleMs` (touch.h, the mobile UI's value too) when handled is
    dropped - a tap's time is its fingers' lift, a long press's its
    recognition (the finger is still down); pans, pinches and ends of what
    began are always handled, keeping gestures whole; two finger pinch moved
    from mouse.cc into touch controls (same rule as one finger: over the map
    while it may scroll);
  - no mouse path with touch-native input: no cursor warp to the finger
    (touch.cc), no emulated clicks, holds, drags or wheel (`_mouse_info`
    reads only a real mouse); the emulation (`mouseEmulateTouch`) runs only
    for the game's windows (`[touch] mobile_ui=0`, the desktop / CE path);
  - mod bridge: `mouseGetPointerPosition` (sfall `get_mouse_x/y`,
    `tile_under_cursor`, `obj_under_cursor`) is the last point touched on the
    map, `mouseGetPointerButtons` (`get_mouse_buttons`) the left button while
    a finger is on the map, `HOOK_MOUSECLICK` runs for taps (pressed,
    released) and long presses (pressed, released when let go - the game's
    held left button opens the action menu too) on the map
    (`inputRunMouseClickHook`); cursor mode and `outlined_object` were the
    targeting controller's already;
  - touch-only build (Android, `FALLOUT_TOUCH_ONLY` in touch.h, 2026-09-30):
    the mobile UI, touch controls and the world view are always on
    (settings.cc), the mouse emulation and its helpers aren't built (touch.cc
    cursor warp, mouse.cc `mouseEmulateTouch`, touch controls' cursor warp
    and UI touch adjusting), a connected mouse is ignored (SDL mouse events,
    `_mouse_info`), the pointer isn't captured and there's no system or game
    cursor; scripts' pointer is always the last map touch (the screen's
    center before one); the game's windows' code stays for the desktop build
    and is never reached;
  - legacy stage 0 (2026-09-30, docs/legacy-audit-2026-09-30.md): on
    touch-only `muiIsEnabled()` and `touchHudIsEnabled()` are `constexpr
    true` (touch HUD forced on too), `touchControlsIsEnabled()` is the world
    view's life (isoInit..isoExit); Android links with `--gc-sections` and
    hidden visibility (only `SDL_main` exported), so the replaced windows'
    code is dropped from the library; the phone APK is the `phone` build
    type (Release);
  - legacy stage 1, commands instead of keys (2026-09-30,
    `game_commands.{h,cc}`): the touch HUD and the mobile screens no longer
    press keys. HUD buttons and screen tabs post typed game commands (menu,
    inventory, character, Pip-Boy, map, skills, sneak, swap hands, item
    action, quick save/load, end turn/combat, the HUD's own actions); the
    main loop and the player's combat turn run one per frame, the game's
    hotkeys (`gameHandleKey`) run the same `gameCommandExecute` (one code
    path, PC unchanged); pending commands drop with pending input
    (`inputEventQueueReset`, e.g. the player's turn start). Screens over the
    game's loops send typed requests the loops check every frame instead of
    wake-up key codes: inventory/loot close (`inventoryRequestClose`,
    `lootRequestClose`), barter offer/talk (`barterRequest`), talk option and
    barter (`gameDialogChooseOption`, `gameDialogRequestBarter`), party
    control actions, Pip-Boy rest stop (`pipboyStopRest`); the
    `*_ACTION_EVENT` codes are gone. Android's back button is Back of the
    mobile UI (`muiRequestBack`: the topmost modal screen's `back()` at the
    next frame, with none the HUD - closes the log, else the game's menu),
    not Esc; screens that relied on Esc got `back()` (inventory, loot,
    barter, talk, scenes, movies, title menu = exit as Esc). Touch-only
    builds drop keyboard events except mods' own (`tap_key`, hotkeys of
    mods' buttons - `sfall_kb_consume_hotkey_event`, they still reach
    `HOOK_KEYPRESS` and `key_pressed`) and a soft keyboard's editing keys
    while a text field is edited. Autotests: `COMMAND` and `BACK` steps
    instead of keys in the mobile scenarios (PC ones keep keys);
  - legacy stage 2, talk and barter without the game's windows (2026-09-30):
    under the mobile UI (`gameDialogWindowless`) the dialog makes no windows
    (background, talk and barter panels, reply, options) and no buttons;
    what the game draws into the background (talking head with its lips,
    fidgets and reaction transitions, or the map around the speaker, the
    screen's highlights and bezel) goes into a 640x480 canvas of the same
    layout, the talk screen shows its head area. The game's functions run
    in the same order with the same scripts, hooks and game mode changes,
    only drawing into windows is skipped; the loop waiting for the player
    is the screen's (`gameDialogRunTouchLoop`: options, barter, party
    control), the game's (`gameDialogRunWindowLoop`) stays for the
    windows. Barter: its tables are made apart from its window
    (`gameDialogCreateBarterTables`), `barterProcessUI` is a session
    (`barterSessionBegin/End`: the trader's armor and weapon set aside,
    hidden items, party targets) with the game's window loop or the
    screen's (`barterRunTouchLoop`), offer and back to talk are shared
    (`barterSessionOffer`, `barterSessionReturnToTalk`); the trade setup
    no longer needs the dialog's barter window (it quit the game without
    one). Autotests compare the logic: `dialogtrace` (Clint: talk, barter
    with a deal and items left on the table, branches) against the build
    before the change, `headtrace` (`--dev-map=arvillag.map`, the Elder's
    talking head) and `bartertrace` between the mobile UI and the game's
    windows (`out/dev/runpc.sh`, `[touch] mobile_ui=0`): reply, options,
    caps, items, checksums of global, map and the speaker's local
    variables are the same, only the windows differ (none now);
  - legacy stage 3, inventory and loot without the game's windows
    (2026-09-30): under the mobile UI (`inventoryScreensWindowless`) the
    inventory, loot/steal and trade make no window and no buttons
    (`inventoryCreateButtons` runs only with a window; the party slots and
    the arrows too), draw nothing, and their loops are the screens'
    (`inventoryRunTouchLoop`, the loot's own branch: queued actions, close,
    stealing caught); the game's loops (`inventoryRunWindowLoop`, the loot
    window loop) are back to the game's own (no mobile hooks). Setup and
    the end (equipped items set aside and back, hidden items, dead critters
    at the tile, party targets, steal counts, experience and the caught
    script call, `_exit_inventory` with dropped explosives) are the same
    code. Checked by `inventorytrace` / `loottrace` (the inventory and loot
    scenarios logging dude's items with container contents, hands, armor,
    AC, HP, AP, weight, caps, the looted side, the ground and global
    variables after every step) against the build before, and
    `pcinventory` (runpc.sh) for the game's windows;
  - legacy stage 4, the interface bar as a model (2026-09-30): with the
    touch HUD (`interfaceBarWindowless`) the bar makes no window, buttons,
    art, side panels or display monitor art; `gInterfaceBarCreated` (not
    the window) says the bar exists, so its logic runs the same: hands and
    their actions, swap hands with dude's put away / take out animation,
    item use and reload, AP to the HUD (`extendedApBarInit` keeps the
    extended AP bar's bulb count apart from its art), end buttons state
    and sounds, indicators, save/load of its state (the same fields).
    Drawing (counters, lights, the main action button, the end buttons'
    animation) is skipped: combat no longer waits for the invisible end
    buttons' animation. Checked by `hudtrace`, `combattrace`,
    `calledtrace` (HUD, combat, called shot scenarios logging the bar,
    items and dude after every step) against the build before, and `pcbar`
    with the touch HUD against the game's bar (`out/dev/runbar.sh`,
    `hud=0`): the same except the moment combat starts (no waiting now)
    and idle fidgets' timing (a fidget ending right after an unanimated
    equip leaves dude's sprite without the weapon and the next attack
    doesn't start - the game's own quirk, the old runs of `calledshot`
    had it);
  - legacy stage 5, the tails (2026-09-30, touch-only build): Ctrl+P's
    pause window isn't handled (it waits for a key or the mouse); the
    interpreter's messages (script errors) and the `gnw` debug mode go to
    the log instead of the game's debug window; map save errors show the
    mobile UI's dialog box (any mobile UI) instead of the game's message
    window; scripts' own windows (`scriptWindowCreate`: `CreateWin`,
    sfall `create_win`) and `selectfilelist` aren't supported - the
    script gets "no window" / nothing chosen, a warning is logged (the
    user's decision; `create_message_window` is the mobile dialog box
    already); the cursor keeps only which one it is (`gameMouseSetCursor`:
    the wait cursors tell the game is busy), its art isn't loaded and
    nothing is drawn (`mouseSetFrame`, `mouseShowCursor`, `mouseHideCursor`
    keep only whether it's shown). `inventoryHasWindow`,
    `interfaceBarHasWindow` and the display monitor's refresh are false
    when building the touch-only build, so the bodies of the inventory,
    bar and monitor drawing functions are left out of it; automated tests
    have no idle fidgets (`_dude_fidget` returns under the autotest): a
    critter fidgeting under a test's computed tap point made the tap miss
    it (a walk instead of a talk), and dude's fidget ending right after an
    unanimated equip dropped the weapon from its sprite - both depended on
    frame timing and made runs differ;
  - autotest `modbridge` (pointer, tile, object, buttons, hook calls after a
    tap and a long press); touch scenarios unchanged (default, magnet, hud,
    ui, combatscreens, calledshot, staletouch, stuckfinger, talk, party,
    barter, perfmap).

## Steps

1. Player commands layer; mouse code moved onto it, no behavior change.
   Autotest: same AP, positions and results by mouse and by command.
2. Combat and skills without the cursor: map touches call commands directly,
   targeting controller, hints (hit chance, move AP). Hex cursor and
   destination marker aren't created on touch.
3. Mod bridge + test script.
4. Mouse emulation off per ported screen, removed at the end.
