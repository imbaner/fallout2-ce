# Android touch adaptation — roadmap

**2026-09-29, GPT-6 Astra:** исправлен вылет перед новым главным меню
после изменения HR-фона в работе Claude Opus 5.5. Причина — именованный
FRM передавался в загрузчик только для числовых FID; исправлены загрузка
через `FrmImage` и ключ кэша. Автотест меню/выбора персонажа и обе сборки
прошли, APK установлен. [Причина, реализация и проверки](main-menu-startup-fix-2026-09-29.md).


Revised 2026-09-29. Technical notes: `android-touch-notes.md`.

## Done

- **Game menu and save/load screen** (Codex / GPT-6 Sol, save catalog by
  Codex / GPT-6 Astra, reviewed and reworked by Claude Opus 5.5 on
  2026-09-29): mobile screens, saves newest first, new saves only (no
  overwrite), deletion, permanent copies of quick saves, quick saves in their
  own range, quick load of the session's last save, transactional writes.
  Details in `android-touch-notes.md` and the
  [save/load handoff](save-load-handoff-2026-09-28.md).
- **Settings screen** (2026-09-29): mobile screen instead of the game's
  preferences window (the window stays for `mobile_ui=0`): sections Игра /
  Бой / Звук / Экран, the game's settings with the port's and CE's ones by
  theme (badges "порт" / "CE"), changes marked with a dot, Apply / Reset all,
  Back with changes asks Да / Нет. Details in `android-touch-notes.md`.
- **Main menu and character selector** (2026-09-29): mobile title menu
  (the game's picture pinned right, Continue with the save's place, the
  game's buttons) and premade character selector (tabs of the characters and
  "+ Own", portrait, biography, stats in two columns, Modify / Play or Create
  character). Details in `android-touch-notes.md`.
- **Combat speed for every combat animation** (2026-09-29): the game's
  combat speed speeds up only walking; the port's enhancement (setting
  `[enhancements] combat_speed_all_animations`) speeds up running the same way
  and attacks, reloads, hits, falls, deaths up to 2x; animations catch up on
  frames a slow device's loop missed. First item of the port's own
  enhancements (see `android-touch-notes.md`).
- **Build**: Android build on macOS (NDK r27, 16 KB pages), game data with
  RPU and Russian localization on the device.
- **World view**: map rendered separately from UI, GPU zoom 0.5x-4x, smooth
  sub-tile panning, zoom-aware picking, map edge limits by visible size.
- **Touch controls**: tap to walk/act, radial action menu, combat with
  confirmation tap, one finger camera, no cursor, destination marker; UI:
  taps hit under the finger, button touch slop, Android back = Esc, one finger
  list scrolling, inventory gestures (tap/long press/drag) with radial menu,
  world map drag/tap, long press = right click on buttons (attack modes).
- **Opening movies**: a legacy menu/character-selector tap was carried into
  the native full-screen player as a synthetic mouse press and skipped Intro
  or Elder immediately. The player now waits for release before allowing a
  later click to skip. `titleintro`, `newgameelder`, and the existing movie
  touch-skip scenario pass on the desktop rig; updated APK installed. Codex on
  GPT-6 Sol (`gpt-6-sol`) completed this follow-up after the Claude Code /
  Claude Opus 5.5 implementation. See the
  [movie handoff](movie-playback-handoff-2026-09-28.md) for cause, changes,
  and test evidence.

## Remaining, grouped by topic

1. **Mobile UI, landscape** (main block)
   - Architecture: every screen's layout is described separately from its
     logic, so each screen can have a landscape and a portrait layout (only
     landscape layouts are made in this block, portrait ones in block 5).
   - Resolution independent HUD: the HUD is split into independent groups,
     each anchored to its own screen corner/edge (offset in dp, safe area
     insets), sized in dp so buttons keep the same physical size on any
     phone/tablet and any aspect ratio; UI size setting multiplies it.
     Layouts are data (one table per orientation), not code.
   - Own touch HUD instead of the original bar: inventory, character,
     Pip-Boy, maps, skills, end turn/combat, quick save/load, highlight items,
     sneak, AP/HP, collapsible message log.
   - Weapon: attack mode switcher (single / aimed / burst) and a separate
     reload button.
   - All screens rebuilt as mobile app screens on the new GPU UI kit
     (`src/mui*.cc`, native resolution, dp, theme for the look later):
     dialog (done), barter with quantity (done), inventory (done), loot (done), skilldex + use item on (done),
     character/perks + character creation (done), Pip-Boy (done), in-game
     options and save/load (done), settings (done), main menu and character
     selector (done), elevator, death screen, ending slides, credits (done),
     world map and town maps (done); mods' script windows: none used
     (checked, see android-touch-notes.md),
     dialog boxes (done),
     radial menu (done). Inventory
     Filter mod features (filters, party tabs, drop/take all) built in. Full-screen where useful, back
     button + Android back + swipe back, inertial lists, drag & drop.
   - UI size setting (done, settings screen).
2. **Picture and readability on the map**
   - Inertial list scrolling: done (mobile UI lists).
   - Map hints: a fixed-size overlay already (map_hints.cc).
   - Floating texts (what critters say, `float_msg`): done 2026-10-07 -
     the game keeps them (text_object.cc: lifetime, count, tile), the
     mobile UI draws them over the map at a fixed size, the hints' 13 dp,
     not growing with the zoom (mui_floating_text.cc): the game's color,
     a thin outline, our font, wrapped about as wide as the game's, kept on
     screen. Autotest `floattext`.
3. **Maps** (done)
   - World map: mobile screen (mui_worldmap.cc), drag and pinch zoom,
     towns list, camera following the party.
   - Automap (Pip-Boy maps, MuiMapView: drag, pinch) and town maps
     (entrances as marks) for fingers.
4. **Controls and settings**
   - In-game touch settings: touch mode on/off (cursor fallback),
     sensitivity, zoom limits.
   - Map off the mouse path, no mouse emulation with the mobile UI (done:
     `touch-architecture.md` steps 3-4, mod bridge, touch controls read
     gestures themselves with the stale-touch expiry; the emulation stays
     only for the game's windows, `mobile_ui=0`).
   - The port's enhancements (`[enhancements]` in fallout2.cfg,
     `EnhancementSettings`) are rows on the settings screen next to the
     game's settings of the same theme (done for combat animations; every
     new one gets its row).
   - (Soft keyboard for names already works.)
5. **Portrait** (most expensive, after landscape)
   - Second variant of the whole mobile UI: portrait HUD and portrait layouts
     of every screen from block 1 (inventory, dialog, barter, Pip-Boy,
     character, options, save/load, maps). Constraint: engine needs >= 480
     logical height and original screens are 640 wide, so on a 720 px wide
     phone they would be only ~1.1x — portrait layouts must be designed for
     the narrow screen, not just scaled.
   - Orientation chosen at start first, then runtime rotation (recreate
     windows with the other layout).
6. **Android app** (2026-10-07)
   - Done: Gradle 8.9 + AGP 8.7.3, compileSdk/targetSdk 35; app id
     `io.github.imbaner.fallout2ce` (the phone build `.debug`, "Fallout 2
     CE (dev)"; the Java package stays upstream's); name "Fallout 2 CE"
     (the icon stays CE's); version "<CE version>-mobile.<n>", code
     major*1000000 + minor*10000 + patch*100 + n; release signing with the
     owner's key outside the repo (`fallout2ce.releaseKeystoreProperties`
     in ~/.gradle/gradle.properties); release native code as the phone
     build's (Release).
   - Done: ce.dat comes with the app (files/ce.dat as a folder in the
     assets, `bundleCeDat`), MainActivity puts it next to the game's data
     when the app is new or updated (`.bundled` stamp); the game's
     resolution on Android follows the screen (480 lines, columns by its
     proportions, full screen; svga.cc), so a config copied from a
     computer works.
   - Done: import screen (ImportActivity + GameImport): the player's own
     copy, a folder or a .zip put on the phone, found there or one folder
     in (master.dat + critter.dat), language/mods/saves/size shown, free
     space checked, copied with progress and cancel (what was copied is
     removed; `.import-incomplete` keeps a partial copy from starting),
     Windows programs and the source's ce.dat skipped, absolute Windows
     paths in fallout2.cfg fixed. Mobile UI colors and font, en/ru; own
     progress (ImportProgressView: fill, or a sweeping segment while
     looking), a lone button spans the panel.
   - Plain Fallout 2 (GOG data, no RPU, no sfall.dat/ddraw.ini) checked
     2026-10-07 with the core autotests (`GAME=out/dev/testgame-vanilla
     out/dev/runtest.sh ...`): every screen works; mod extras (Party Orders
     button) show only with their mods; Russian needs a Russian translation
     of the game (RPU's or 1C's), the port's own texts are in ce.dat.
   - The build with the game inside (`withGame`, the .dat files read in the
     APK) removed 2026-10-07: one layout on every phone - the app plus the
     game as a plain folder, as on a computer. Builds with the game for the
     owner's own phones may be made again one by one, never as the way the
     port is given out.
   - Pause/audio on background: works (checked by the user 2026-10-07).
   - Performance on the device: good (the user, 2026-10-07). The release
     builds without the always-on touch recorder and game journal
     (`FALLOUT_DIAGNOSTICS=OFF`; the config can turn them on); the phone
     test build keeps them.
   - Game files (done 2026-10-07, see "Game files" below).
   - Later: several games on one phone (Fallout 2, Nevada, Sonora...), each
     with its own folder and saves.
7. **Localization**
   - (Russian wide inventory/loot backgrounds: obsolete - they were CE's
     legacy windows' art, the mobile screens replaced them.)

## Game files (agreed and done 2026-10-07)

On the phone: the app (engine, the port's ce.dat) and the game as a plain
folder, as on a computer (the original files, mods, configs, saves). Nothing
is unpacked out of the APK (that would take the space twice), there is no
"game package" with versions to keep: whatever is needed is worked out from
the files. CE and its sfall support come with the app; ddraw.dll, sfall's
Windows part, is never used. The build with the game inside (withGame) is
gone.

- Import from any folder or archive: src/import_archive.cc (zip read
  there: stored, Deflate, Deflate64 through zlib's contrib/infback9, zip64,
  CP866/UTF-8 names; .7z, .rar 4/5, .tar.gz/.xz through libarchive built by
  third_party/libarchive with liblzma of third_party/xz), the app's
  `fallout2-import` library (import_archive_jni.cc, NativeArchive.java).
  Clear messages for encrypted, split or cut off archives and methods not
  read. The game found at any depth (master.dat + critter.dat, the
  shallowest; several at one depth - asked to choose); Windows programs,
  macOS leftovers and .app folders skipped. Checked on the Mac with
  `ce-import-tool` (list/extract) on archives of the real game made by
  zip, 7-Zip, bsdtar and libarchive's own RAR samples.
- **Replace game** (GameFiles.java): the only operation on the game. The
  source's files become the game's (removed what it lacks); without
  master.dat and critter.dat the original files stay (only the mods are
  replaced). Copied into a staging folder next to the game first (the same
  file isn't copied: compared while read), then moved in by a journal
  finished on the next start if the app dies (MainActivity). Space counted
  first. fallout2.cfg keeps the player's settings (all but [system] and the
  music paths). Saves are never touched (the source's aren't taken: "Import
  saves" is for them). The game closes first (ImportActivity runs in its own
  process and waits for it).
- Saves: data/SAVEGAME stays exactly as on a computer. What the port knows
  of each save - when it was made (the list's order), what the game was
  made of then - is in its records (src/save_records.h: the app's private
  `saves.txt`, by slot and the save's identity, a fingerprint of SAVE.DAT).
  Earlier builds' CE-META.TXT in the slot folders is moved there once and
  removed. A save moved away and back keeps its record; deleting it drops
  it. Export (GameSaves.java): one zip, SAVEGAME plus `fallout2-saves.txt`
  (the records and which saves were quick) beside it. Import: a folder, an
  archive of SAVEGAME, of SLOT folders or of a whole game; the list with
  marks and a summary, then Add (the same save skipped, free slots, quick
  saves become normal ones) or Replace all (warned, "export first"
  offered; quick saves to the quick slots, newest first). A computer's
  saves get their day from SAVE.DAT's header; with a whole game whose
  archives (names, sizes) are the installed one's they count as made with
  it, else their compatibility is unknown.
- Save compatibility (`[enhancements] save_compatibility`, a settings row):
  the game's composition (src/save_compatibility.cc) - every archive the
  engine opened (not ce.dat) by a fingerprint of its index and its kind:
  Fallout 2's own, game logic (proto/scripts/maps/data), looks only. One
  comparison (src/save_composition.cc) for the game and the app's import.
  Marks on the save screen: none - works, ? - a mod with logic updated or
  added, x - Fallout 2's files changed or a mod with logic gone (loading
  asks first), "Compatibility unknown" in the details. The first time all
  saves there get the game's composition. Checked on the Mac (a logic mod
  removed: x and "Mod missing", added: ?, a looks-only mod: nothing).
- Settings, section "Game files" (the fifth, Android only; autotest
  `gamefiles` on the Mac): installed mods and saves, Replace game, Import
  saves, Export saves, Delete all saves (asks, export suggested). Replace
  game asks to close the game (its files can't change under it) and starts
  it again after; the saves' import and export open over the game (paused
  behind, it reads the saves and their records again when back, no
  restart); deleting is in the game. The app's screen: full screen, the
  game's back button at the top right (opened from the game), the middle
  scrolls (title and buttons stay); "done" shows for a moment and goes back
  by itself. From the main menu too (its Settings).
- Checked on the phone 2026-10-08 (a test copy, app id
  `.importtest` - `-Pfallout2ce.phoneIdSuffix=.importtest` - beside the
  player's): the first import of a 1.6 GB zip, saves imported from a bare
  SAVEGAME zip (35) and from the app's export (all already there; Replace
  all, the quick save to the quick slots), the export, Replace game with
  mods only (npc_armor gone: the quick save made before marked x, the
  originals kept) and with the whole game again (454 MB copied, 25 s),
  Delete all saves. The player's saves untouched (checksums). Found and
  fixed there: the saves' list hid the buttons (5 lines now), a computer's
  saves showed a made-up time (none now), Replace game's summary tells
  which folders lose files.
- Later: several games on one phone (Fallout 2, Nevada, Sonora...), each
  with its own folder and saves.

## Keeping up with CE upstream (2026-10-08)

The port started from a ZIP of github.com/fallout2-ce/fallout2-ce at
79bdb28a (2026-09-24, found by the files' dates and contents). `main`
now continues that history: the port is one commit over 79bdb28a, so
updates are ordinary merges:

    git fetch upstream            # https://github.com/fallout2-ce/fallout2-ce
    git merge upstream/main

First merge (2026-10-08, up to 85134d30): 29 upstream changes, 22
conflicts in 14 files. Most came from upstream's typed ids (ProtoId,
FrmId, scoped enums): the port's code now uses `ProtoId(obj) ==
ItemProtoTypeId::...`, `frameId<CritterFrameId>()`,
`GameMouseActionMenuItem` for action menus. Points where upstream's
features had to be carried into the mobile screens:

- main menu "behind submenus" (`mainMenuBeginSubscreen`...): PC as
  upstream; the mobile menu creates no backdrop window and keeps
  Continue and the loading curtain;
- holodisk narration: the mobile Pip-Boy plays it when a holodisk is
  shown and stops it when leaving the holodisks (as upstream's window).

RPU had no release after 2.4.34 (checked 2026-10-08).

## Navigation and screen layout rules (2026-09-29)

Agreed while designing the settings screen:

- Right column: moving between screens - Back (always at its top, a square
  like on every full screen) and the sibling game screens (inventory,
  character, Pip-Boy, map) as icons.
- Sections inside one screen: a text list on the left (settings: Game,
  Combat, Sound, Display...), the chosen section's content to its right.
  The usual layout of game settings; text because the names matter more
  than icons.
- To rethink in the final navigation pass: Pip-Boy has sections inside one
  screen too (quests, data, maps, videos, rest) but needs its horizontal
  space for several columns (a quest list + text, maps + list), so a left
  section list would take room it uses. Options: sections as a top row,
  a collapsible left list, or keeping its current layout as the documented
  exception. Decide with the settings screen as the reference.

## Settings screen follow-ups (2026-09-29, settled 2026-10-08)

CE's `[qol]` / `[ui]` features in the mobile screens:
- karma change messages (`display_karma_changes`, Russian text in
  `game\ce.msg` 324-325) and bonus damage in the stats
  (`display_bonus_damage`, read when shown) are rows (Game, Combat), off by
  default as in CE and sfall/RPU;
- quick saves (`auto_quick_save`): a slider 0-100 by tens in Game ->
  Convenience (0 - off: the quick save button opens the save screen). The
  saves are rearranged first (`SaveCatalog::setQuickRange`): more quick
  saves - manual saves in the new range move out; fewer - the oldest quick
  saves become manual where they are in the list, after a question that
  says so (and that the button opens the save screen when off). Every move
  is one folder rename right for the old and the new range, the setting is
  written last, records follow their saves (`updateRecords`). The save
  screen's button "Make permanent" moves a quick save the same way (no
  copy). Autotest `quicksaves`, unit test in save_catalog_test;
- numbers in dialogue: not needed on a touch screen;
- use walk distance: engine logic, works;
- loot screen: the target's carry limit and a container's fill are not
  shown (the player's weight and limit are) - open, "useful, not urgent".

## Map rendering towards 60 fps (postponed, 2026-09-29)

2026-10-08: the release build (no diagnostics) runs close to 60 fps on
the Redmi 15C; the user finds it good. The steps below stay as a plan for
weaker phones, not as current work.

Where it stands: needless redraws are gone (only the view is drawn, roofs
redraw their tiles, pinches recenter once, animation refreshes merged; see
`android-touch-notes.md`). The phone (Redmi 15C) is playable, but fast
camera moves zoomed out still drop below 60 fps. The cost left is the
drawing itself: the map is drawn by the CPU into the 8-bit world buffer
exactly like the original engine (floor tiles, then objects with light and
translucency, roofs), converted with the palette and uploaded to the GPU.
Moving the camera brings new map into view that has to be drawn; zoomed
out that's up to 4x the area for the same finger move. perf.log of the
phone: the "logic" column (CPU drawing) is what grows, uploads are small.

Measure before and after each step with `perfmap` on the phone (the
player's own save, read only):
`adb shell am start -n io.github.imbaner.fallout2ce.debug/com.alexbatalov.fallout2ce.MainActivity --es args "--dev-autotest=<files dir>/<out> --dev-load-game=<slot> --dev-autotest-scenario=perfmap"`
(ms per frame, map pixels drawn by the CPU, uploaded to the GPU, per part:
drag at 1x, pinch, drag at 0.5x to the edges, walking, roofs), plus
perf.log of a normal play. The perf.log of the build of 2026-09-29 is the
reference.

Steps, most gain first:

1. **Floor on the GPU (do first).** Floor tiles almost never change (only
   when a map script swaps a tile, or the elevation/map changes). Draw the
   floor of the explored part once into large GPU textures (world pixels,
   palette-converted, e.g. 1024x1024 pages) and draw the visible pages each
   frame, scaled like the world texture now; the CPU buffer then gets only
   what stands on the floor. Scrolling and zoom cost nothing for the floor,
   which is most of the screen's pixels. What to watch: objects are
   drawn over the floor with their own transparency and the floor's light
   (`tileRenderFloor` uses the ambient light and light grid): the object
   layer needs alpha (index 0 transparent) instead of drawing into the same
   buffer; light changes (day/night, light sources) must update the floor
   pages (redraw them, or keep light as a separate multiply layer); EDG
   edges (black outside the map) and the grid overlay; color cycling
   entries in floor tiles (water) - pages remember where cycling indices
   are, like `world_texture.cc` tiles. Closest to today's architecture
   (`world_texture.cc` already composes GPU tiles); biggest gain per work.
2. **Objects on the GPU.** Critters, walls, scenery and items are ready FRM
   frames: keep them as textures (atlas per FRM, palette indices + palette
   lookup in a shader so cycling and fades stay palette operations) and draw
   them in the engine's order (`_obj_render_pre_roof` / post roof, flat
   first) with a shader for the original's light (intensity per tile from
   the light grid), translucency types (glass, energy, red/steam/wall
   transparency) and the egg (see-through circle around the player). The
   largest step: the CPU renderer stays for the PC fallback and as the
   reference (compare frames pixel by pixel in autotests). Gain at every
   zoom, including animations (no CPU redraw of moving critters).
3. **Faster CPU drawing (smaller, quicker).** Profile `tileRefreshGame` on
   the phone (simpleperf) - floor blits, `_dark_trans_buf_to_buf` /
   intensity lookups for lit objects, roofs - and rewrite the hottest loops
   for ARM (NEON, fewer per-pixel branches). Independent of 1-2, can be
   done any time; gains shrink once 1-2 move work to the GPU.
4. **Fades on the GPU (not the camera, map transitions).** A palette fade
   changes every palette entry, so each fade step re-uploads the whole
   world (~2.4 Mpx) and the screen (`colorPaletteFadeBetween`, steps are
   already skipped when frames are slow). A fade to/from black is a
   uniform scale of the palette: draw with a color modulation instead of
   re-uploading (keep uploading with the base palette, cycling entries
   included), only real palette swaps re-upload.

## Future ideas (decide later)

- **Pointer to the party off screen** (considered 2026-09-29, not added): a
  round mark at the screen's edge pointing to the party, a tap brings the
  camera back. It changes no balance (only the player's own position), but
  the world map follows the party and fits on the screen, the local map keeps
  the party in view, and at the edges it would fight the HUD's buttons
  (menu, quick saves, inventory, the attack panel) - more complexity than use.

- **Touch help** (2026-09-29): the game's F1 help is a picture of the
  keyboard and mouse controls and isn't shown under the mobile UI. A help of
  the touch UI's gestures and buttons could take its place (from the game
  menu).

- **Wider main menu art** (2026-09-29): the mobile main menu shows the
  game's MAINMENU.FRM (640x480, its button plates baked in) cropped to the
  helmet, fit to the screen height and pinned to the right edge; the
  buttons stand on the left over the plain background. Later the picture
  can be extended to the left with AI (background, the shoulder), the
  helmet staying where it is. The screen takes the art as "an image of any
  width with the part to show", so a wider truecolor image (PNG in ce.dat)
  replaces the crop without layout changes: it's still height-fit and
  right-pinned, only more of it shows on wider screens.

- **"Ground" tab in the inventory**: items lying within ~2 hexes of the
  player, reachable only (like Party Orders' `can_reach`), out of combat
  only (in combat picking up costs AP and needs walking). Taking and
  dropping go through the game's own actions (`objectPickup` /
  `objectDrop`: weight limit, item pickup scripts, sfall inventory hooks),
  identical items shown as one stack, "take 40" = 40 ordinary pickups.
  Motivation: a dropped stack lies as separate objects and is picked up one
  by one (original behavior). Existing alternatives: Party Orders "pick up
  items" (3 hexes, one per game second), Auto Loot mod.

- **More view gestures** (2026-09-27): double tap rejected by the user
  after the mockups (2026-10-08: "точно нет"); inertia after dragging
  undecided — for the world view and the Pip-Boy / automap
  maps together, so gestures stay the same everywhere (today: drag and pinch
  only).

- **Map scroll hitches** (zoomed out, near map edges): every scroll step
  the game draws the uncovered strips of the world buffer in software
  (32 x buffer height, buffer width x 24, more steps a frame when zoomed
  out); on map edges with EDG data `tileSetCenter` sets sub-tile alignment
  and redraws the whole buffer each step. Options: spread strip drawing over
  frames / draw the visible part first; rework edge alignment so it doesn't
  need full redraws. Roof toggling also redraws everything.

Suggested order: 1 -> 2 -> 4 -> 6 (fully playable landscape game), then 3, 7,
and 5 as a separate big block.

Bottleneck is on-device testing: touch input can't be injected on the user's
Xiaomi (needs SIM for "USB debugging (security settings)"), so every screen
needs a manual pass.
