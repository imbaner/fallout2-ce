# Mobile save/load: 2026-09-28 handoff

## Provenance and scope

The Android touch port and its GPU mobile UI were developed in the user's
earlier Claude Code / Claude Opus 5.5 work, which ended with the approved
mockup of the game menu and the save/load screen. Both screens
(`mui_options.cc`, `mui_loadsave.cc`) were then written by Codex / GPT-6 Sol. On
2026-09-28 the user asked **Codex running GPT-6 Sol (`gpt-6-sol`)**, a different
model, to finish this save/load task. This document records that follow-up;
it does not attribute the whole port or the original picker to GPT-6 Sol.

The later save-history/retention/session-quickload implementation below was
completed by **GPT-6 Astra (`gpt-6-astra`)**, as clarified by the user on
2026-09-29. Attribution is recorded separately for each stage.

The request was to remove the redundant Load tab in the title-screen flow,
make deletion available with a confirmation, fix black/flickering legacy save
previews, and improve the save-list text. The previous design feedback also
required the right detail panel to reach the top, the action buttons to stay
within it, and the right-side rail to remain free for Back.

## Implementation

### Layout and interaction (`src/mui_loadsave.cc`)

- `LoadSaveScreen::build` uses the same `muiDialogLayout` safe area and right
  rail as other new screens. With `fromMainMenu`, `tabsHeight` is zero, so the
  list begins at the top and there are no Save/Load tabs. In-game, the two
  tabs remain above the list. The detail panel starts at the top of the
  content area. Back remains in the right rail.
- The occupied-slot title is 15 dp, fitted down to 11 dp only if required by
  width. The secondary place text is 12 dp. `+ New save` uses the full row
  height for vertical centering. An empty load list has a centered message.
- Delete sits to the left of Save/Load in the detail panel. It appears for
  occupied, corrupt, old-version, and quick-save slots. A corrupt or
  old-version slot has only Delete because it cannot be loaded. The temporary
  `New save` row has no Delete button. The primary action never extends into
  the Back rail.
- Touch sets `pendingDeleteSlot`; `runPending` opens the existing native
  mobile yes/no dialog after the frame. It clears any pending Save/Load
  action, shows the slot number and available description, and leaves the
  slot intact on Cancel. After confirmation it invokes the slot API, shows
  an error dialog on failure, and refreshes the list and selection. This
  reuses the project's `MuiScreen`, `MuiContext`, and `showDialogBox` paths;
  it does not route touch through the old mouse picker.

### Safe slot deletion (`src/loadsave.cc`, `src/loadsave.h`)

`lsgMobileDeleteSlot` validates the slot index, refreshes the save cache,
rejects an empty slot, and builds only its exact `SAVEGAME/SLOTnn` path with
bounded buffers. It checks that both `SAVEGAME` and `SLOTnn` are real
directories, not symlinks or Windows reparse points. The recursive walk
uses `lstat` on POSIX and file attributes on Windows, so a link inside the
slot is removed as a link rather than traversed. It checks removal errors,
removes the slot directory itself, and refreshes the cache again. Removing
the whole slot also removes `PREVIEW.PNG`, maps, prototypes, and mod sidecar
files. The method accepts a corrupt or unsupported-version slot because
neither can be loaded but both still occupy a save slot.

The source uses POSIX/Win32 directory APIs for compatibility with the
project's macOS deployment target. An initial `std::filesystem` version did
not compile for that target; an intermediate file-finder approach missed
entries in the test fixture. Both were replaced before final verification.

### Legacy preview rendering (`src/mui_loadsave.cc`)

Old `SAVE.DAT` thumbnails contain 224 x 133 palette indices. The old display
path used the active palette, which can be black during title-screen fades,
and could make index-zero edge pixels transparent. `convertLegacyPreview`
now converts the thumbnail once per selection from the stable Fallout base
palette (`_cmap`) into opaque RGBA and draws it via `muiRgbaTexture` in the
clipped preview frame. The preview retains the existing cover/crop layout
and small inner margin. Newer saves still use their widescreen `PREVIEW.PNG`.
The legacy `SAVE.DAT` thumbnail format remains compatible with the game.

### Text and test entry points

- English and CP1251 Russian `files/ce.dat/text/*/game/ce.msg` gained ids
  234-239 for Delete, confirmation, failure, and the empty-list message.
  `ce.dat` was rebuilt for the device.
- `src/dev_autotest.cc` gained `muiloadsavedelete`, `muiloadsavemain`, and
  `muiloadsavemainempty`. `src/mainmenu.cc` calls `devAutotestTick()` from
  the title-screen loop so the last two exercise the real entry path.

## Verification performed

- macOS: `cmake --build out/dev/build --target fallout2-ce -j6` passed.
- Android: `JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
  ./gradlew assembleDebug -PabiFilters=arm64-v8a` from `os/android` passed.
- `out/dev/results-muiloadsave-final2/log.txt`: normal save/load scenario
  reached `autotest finished`.
- `out/dev/results-muiloadsave-main/log.txt`: title-screen Load scenario
  finished. `t01_load.png` shows the list at the top and a visible legacy
  preview. Preview pixels in two captured frames were identical.
- `out/dev/results-muiloadsave-main-empty/log.txt`: empty title-screen Load
  scenario finished; `e01_load_empty.png` shows the empty-state message.
- `out/dev/results-muiloadsave-delete3/log.txt`: Cancel left slot 1 occupied
  (`1`); Confirm made it empty (`0`). The isolated fixture included a nested
  file and a symlink to an external sentinel. The complete slot directory
  was removed and the sentinel remained. `d04_delete.png` shows the dialog;
  `d11_save.png` shows the freed slot. The fixture was restored afterward.
- `out/dev/results-muiloadsave-delete-corrupt/log.txt`: the same delete flow
  worked for a deliberately corrupt `SAVE.DAT`; the original fixture was
  restored afterward.

The APK was copied to `~/Downloads/Fallout2-Android/fallout2-ce-debug.apk`,
installed with ADB, and copied to the phone's Download directory. Rebuilt
`ce.dat` was pushed to
`/sdcard/Android/data/io.github.imbaner.fallout2ce.debug/files/ce.dat`.
`MainActivity` launched and the process stayed running; the checked error
log showed no crash. MIUI prevents automated ADB input injection on this
phone, so the actual finger flow still needs a manual device pass. The
automated touch scenarios ran on the macOS test rig.

## Boundaries for the next contributor

These changes belong to the current native-resolution GPU mobile UI and use
its dp sizing, safe-area layout, text drawing, texture cache, and touch
widgets. Keep new save/load work there. The classic picker remains available
when `[touch] mobile_ui=0`; it was not removed by this follow-up. The title
menu itself is still a legacy window even though its Load action opens the
new picker. Do not count the title menu as migrated.

## Follow-up: black world after loading from the title menu

The user reported that a save loaded from the initial menu showed a black
world with only cycling torch pixels visible. Loading another save from that
state kept the world black; creating a new game first made the same saves
render correctly. The save data was intact. The title menu fades the game's
indexed palette to black in `mainMenuWindowHide(true)`. The classic picker
restores `color.pal` and fades to `_cmap`, but the mobile branch in
`lsgLoadGame` returned before that code. The world texture consequently
converted most indices to black; cycling palette entries could still change.

`src/loadsave.cc` now restores `color.pal` and fades to `_cmap` after a
successful mobile load from the title menu. Cancel leaves the normal title
menu return path alone. `src/dev_autotest.cc` adds
`muiloadsavemainresume`, which actually loads slot 1 from the initial menu,
checks that non-cycling palette entries have color, and captures the world.
The same screenshot region had 0/300,000 nonblack pixels before the fix and
286,344/300,000 afterward. The new test and the existing title-menu Cancel
test both finished successfully. macOS and Android builds passed. The new
APK was installed on the phone and copied to Download; `MainActivity`
started. A manual finger pass on the phone remains necessary because MIUI
blocks automated ADB input injection.

## Follow-up: map taps ignored after the title-menu load

After the palette fix, the user found that map taps still did nothing until
using Sneak or opening and closing Inventory. The title-menu load test
reproduced it: touch controls and world hit testing were enabled, but
`gameMouseIsMapInputEnabled()` was false. The load path left the engine cursor
at `MOUSE_CURSOR_WAIT_PLANET` (value 25), which is intentionally rejected by
the shared map-input gate. The classic picker clears its busy cursor in
`lsgWindowFree`; the mobile picker had no matching cleanup.

`muiLoadSaveScreenRun` now resets that cursor to Arrow when the native picker
closes. After a successful load, it also clears native touch selection state
with `touchControlsReset`. Map touches still go directly through
`touchControlsHandleGesture` and `player_commands`; this fix only restores
the engine's permission to accept map input. The test now verifies the
palette and map-input gate, performs a real synthetic map tap, and fails if
the player does not move. Before the fix the gate was `0`; afterward it is
`1`, and the player moved from tile 17488 to 18091. The test finished
successfully on the macOS touch rig.

## Follow-up: menu flashing during an in-game load

The user reported a few frames of the in-game options menu between tapping
"Load game" in the picker and seeing the loaded world. `LoadSaveScreen` was
removed from the GPU UI stack immediately before the synchronous
`lsgLoadGameFromSlot` call. Any frame presented by the loader consequently
revealed the options screen beneath it.

The picker now stays on the stack throughout the load. It renders a compact,
opaque, localized "Loading save..." plate over a dimmed picker and ignores
picker input during that operation. The plate is presented once before the
blocking load call; subsequent engine frames keep drawing it. After a
successful load, the picker and options screen close synchronously before the
next presented world frame. The failure dialog still appears over the picker.
English and CP1251 Russian `ce.msg` use text id 240. The scripted desktop
test captures the transition frame as `loadsave_loading.png` for visual
inspection. Both `muiloadsave` and `muiloadsavemainresume` passed after the
change; the title-menu scenario also verified that a direct map tap moves
the player after loading. macOS and arm64 Android builds passed. The new APK
and `ce.dat` were installed on the connected phone, and the APK was copied
to Download. Manual finger verification of the transition remains useful.

## Follow-up: chronological save list and phone backup cleanup

The phone's active `data/SAVEGAME` held exactly `SLOT11` through `SLOT20`.
`auto_quick_save=1` and `auto_quick_save_page=1` cycle over those ten slots;
the filesystem timestamps showed `SLOT18`–`SLOT20` were written before the
cycle returned to `SLOT11`, then reached `SLOT17`. The older `SLOT01` and
`SLOT02` existed only in a separate development archive named
`data/SAVEGAME_backup`, which the game never read. The user requested that
archive be removed; it was deleted on the phone, and the active ten slots
were checked afterward. The temporary debug-only cleanup code used for this
operation was removed before the final APK build.

The native picker already scans all 1000 game slots and displays every
nonempty slot in a scrollable list. It now reads each `SAVE.DAT` filesystem
modification time via `compat_file_modification_time`, sorts rows newest
first, resets scrolling to the top when reopened, and initially selects the
newest row in Load. In Save, the empty "New save" row stays above the sorted
existing saves. Slot numbers remain visible and unchanged; game save/load
and the ten-slot automatic quick-save rotation are unchanged. Missing or
equal timestamps retain slot-number order.

macOS and arm64 Android builds passed. The `muiloadsavemain` autotest
finished; `out/dev/results-muiloadsave-sorted/t01_load.png` shows `SLOT08`
then `SLOT07`, matching their file timestamps, with `SLOT08` selected.
The full `muiloadsave` save, overwrite-cancel, and load scenario also
finished without missing controls against an isolated empty save folder;
its log is `out/dev/results-muiloadsave-sorted-flow-isolated/log.txt`.
The final APK and current `ce.dat` were installed on the connected phone;
the process started, all ten active slots remained, and
`SAVEGAME_backup` was absent. MIUI still prevents an automated finger pass
through the phone's picker.


## Follow-up: save history, configurable retention and session quickload

Implemented by **Codex / GPT-6 Astra (`gpt-6-astra`)**, continuing the earlier
Claude Opus 5.5 and GPT-6 Sol work at the user's request. Model attribution was
clarified by the user on 2026-09-29; this section previously said only GPT-6.
The earlier Sol-attributed fixes retain their original attribution. This
section supersedes the earlier statements that visible slot numbers and the
quick-save rotation were unchanged.

### Why this follow-up was needed

The physical slot cursor was doing several unrelated jobs: UI selection,
manual save/load target and next quick-save position. Loading an older record
could therefore redirect the next quick save, instead of retaining the newest
configured number of quick snapshots. The chronological UI also still exposed
physical slot numbers, and file timestamps alone did not give reliable ordering
for records created with equal times or after a clock change.

The user requested a shared storage policy with configurable quick/manual
ranges, reuse of manual holes, chronological display ranks, removal of native
manual overwrite, and full permanent copies of quick snapshots. The user then
explicitly corrected the proposed quickload policy: it must follow the latest
successful load or newly created gameplay save in the current session, whether
manual or quick, rather than always choosing the newest quick snapshot.

A separate catalogue implements these rules above the existing CE serializers
and sfall data handlers. Complete-directory transactions protect all files of
a replaced save, including mod sidecars; the native UI reuses this shared
policy instead of implementing its own storage/counter logic.

### Behavior

- `SaveCatalog` owns placement, chronology and the current session's load
  target; it does not depend on `_slot_cursor`, which remains an engine detail.
- The configured quick page/count define the quick range. Manual allocation
  scans the complement for its first vacant physical address. Zero disables
  automatic quick placement; a range running past slot 1000 is clipped.
- Quick save uses a vacant address first, then the oldest valid quick snapshot.
  Loading an old save never rewinds this selection. Unknown/corrupt entries
  occupy their address and are not silently reused.
- Every successful load establishes the quickload anchor, including a manual
  save or an older quick save. Each successful new save updates the anchor.
  Opening/cancelling a picker, failed saves, and creating a permanent copy do
  not update it. A normal game reset/new session clears the history; the reset
  inside loading does not. If an anchor is deleted, the preceding surviving
  session entry is used; without one, quickload opens the picker.
- Native UI uses chronological ranks, newest first, and a pinned New save row.
  Selecting an old record in Save offers New save, not destructive overwrite.
  A permanent copy action is shown only for a valid quick record, with room in
  the manual range. It copies the entire directory (maps, prototypes, sfall,
  CE sidecars, preview and mod files), without loading/reserializing the world.
  Original quick data remain intact. A copy is a new list entry dated now.
- Physical widget IDs remain stable for touch dispatch/tests; their numbers
  are not shown to the player. Real creation date/time is shown in details.
- Preview capture works when opening Load in-game and then switching to Save;
  touch reset uses the final tab, including Load reached from the Save tab.

### Storage and compatibility

`src/save_catalog.{h,cc}` provides the engine-independent policy;
`src/save_storage.{h,cc}` provides small native file operations compatible with
our existing macOS 10.13 target and Android. `loadsave.cc` retains the original
CE serializer, loader and sfall data handlers. `SAVE.DAT` is unchanged.

Optional `CE-META.TXT` records UTC creation time, a monotonic ordering value and
an identity derived from SAVE.DAT's timestamp, size and header. Legacy saves
use native file timestamps, including subsecond ordering when available.
External replacement invalidates stale metadata; lowercase Windows-origin
slot/file names are recognized. This does not recover chronology already lost
by external file copying. It is not a new guarantee of full cross-engine mod
compatibility.

A write temporarily moves the complete old directory into `.ce-write-N`,
serializes into the normal slot, flushes the complete new snapshot, and commits
with a renamed marker. Startup/refresh rolls back interrupted writes. Completed
transactions are detached as `.ce-cleanup-N` before deletion, so a crash during
cleanup cannot restore a half-deleted backup. These are temporary transaction
artifacts inside the single SAVEGAME, not a second active save store. Native
case-lookup caches are invalidated after these operations. sfall sidecar open,
write and close failures now fail the save instead of silently succeeding.
Directory fsync is used where supported; Android emulated-storage filesystems
may reject directory fsync, which is handled separately from file flush errors.

The existing phone configuration remains one quick page starting at page 1
(physical 11–20). There is no hardcoded dependency on these numbers. No existing
phone slots/config were moved or reclassified. Changing reservations around an
existing collection is a separate migration operation; this update does not
pretend that changing a config key automatically relocates existing saves.

### Verification

- `tests/save_catalog_test.cc`: retention, loading old saves, mixed manual and
  quick session anchors, manual holes, copying all sidecars, rejection of manual
  duplication, write failure rollback, restart recovery, interrupted cleanup,
  external replacement, lowercase names, arbitrary/disabled/clipped ranges,
  capacity and unknown slot contents. Passed on macOS and as a native arm64
  test executable on the phone's external storage. Disposable phone test files
  were removed afterwards.
- `savehistory` engine/native-touch autotest uses an isolated desktop SAVEGAME:
  actual save/load round trips with different character names prove the session
  behavior and that a permanent copy contains the selected old state. It also
  exercises native Copy and New save (without overwrite) and checks world/input
  readiness. Original desktop saves are restored afterwards.
- `muiloadsavemainresume`: loads the default newest row from the title menu;
  verifies 227 colored non-cycling palette entries and actual character movement
  after a synthetic touch. Removed the obsolete assumption that physical slot 1
  is necessarily visible in a chronological list.
- Logs/screenshots: `out/dev/results-savehistory` and
  `out/dev/results-savehistory-title`. `log-final.txt` records the final run.
- Both macOS and Android arm64 builds pass. Android builds use JDK 17.
- A read-only deployment snapshot of the phone's existing saves/config is kept
  locally in `out/dev/deploy-savehistory`; no SAVEGAME_backup was added to the
  phone. The phone's ten existing save directories are preserved.

Deployment completed: `adb install -r -t` returned Success; updated ce.dat was
pushed to the debug app's external files directory. The final APK is also at
`~/Downloads/Fallout2-Android/fallout2-ce-debug.apk` and the phone's
Download directory. The app was launched (PID 25259) and its startup log had no
fatal markers. SHA-256 comparison confirmed all 364 pre-existing phone save
files unchanged. Deployment hashes and evidence are in
`out/dev/deploy-savehistory/deployment.json` and `phone-start-logcat.txt`.

Final `savehistory` and title-resume runs both completed with no FAIL or
NOT FOUND entries in their `log-final.txt`. Gameplay UI/input automation ran on
macOS; the storage suite additionally ran natively on the physical Android
phone. Full gameplay touch automation on the phone was not performed.


## Review pass: 2026-09-29 (Claude Code / Claude Opus 5.5)

Claude Opus 5.5 reviewed the Sol and Astra work at the user's request and
reworked it without changing the behavior the user approved (list order,
no overwrite, quick ranges, session quick load, copies, deletion). This
section supersedes the statements above where they differ.

### Fixed

- A load that failed in the mobile screen left a half loaded world. Now
  `lsgMobileLoadGame` does what the game's window does: the game's error
  (LSGAME.MSG 134/135), `mapNewMap`, back to the main menu. Autotest
  `muiloadfailed` loads a cut copy of the test save.
- Quick saves wrote a hard-coded English "QUICK: dd/mm/yyyy ..." into the
  save's description, and every manual save got "New save" as its
  description, so the list was full of identical titles. Now both have no
  description by default; the list shows where the save was made (and the
  game's date under it), the description field shows that place as a hint.
  Saves already made keep their text.
- A quick save that found no slot to write failed silently, and in the
  game's code a failing quick save write was reported as a success (the
  write's result was dropped). Both now show the game's error (132/133).
- "Game Saved." showed before the catalog committed the save; it shows after
  the commit now.

### Performance

The catalog checked all 1000 slots (3 file system calls each) on every
refresh, and a quick save made 3-4 refreshes, opening the screen 2 plus
1000 file size lookups. Measured on the Redmi (app storage, 20 saves): 69 ms
per refresh. Now a refresh lists SAVEGAME once and reads only the slots in
it (0.7 ms), recovers only slots with `.ce-write-N` / `.ce-cleanup-N`
folders; writes, removal and copies touch only their slot; the game's slot
list skips slots the catalog found empty.

### Structure

- Removal moved into the catalog (`SaveCatalog::remove`: one rename to
  `.ce-cleanup-N`, then deletion; an interrupted one is finished by the next
  refresh) instead of platform code in loadsave.cc.
- loadsave.cc: one path for saving to a slot (`lsgSaveInSlot`, used by quick
  saves, the mobile screen and tests), one for the game's error box
  (`lsgShowError`), LSGAME.MSG loaded for the mobile screen's lifetime
  (`lsgGetMessage`: broken/old save texts 112/113, errors). The wide preview
  uses the game's palette (`_cmap`), not the screen's one that may be faded.
- ce.msg: 224, 225, 228-233, 235 removed (unused or the game's own texts).
- Screens rewritten in the project's style (named text ids, braces,
  comments, no dead portrait layout); `muiOptionsScreenRun` /
  `muiLoadSaveScreenRun` declared in mui.h, `muiPlaceText` shared in
  mui_screens.h. Unused `compat_file_modification_time` removed.
- `tests/save_catalog_test.cc` no longer needs std::filesystem (the macOS
  10.13 target), builds as the `save_catalog_test` target and runs by
  `ctest`; it also checks removal (links not followed) and an interrupted
  removal.
- Autotests changing saves prepare their own SAVEGAME (the test game's saves
  wait in `SAVEGAME.autotest` and come back at exit); `runtest.sh` with
  `TITLE=1` starts at the title screen.

### Verified

macOS: `ctest`; `savehistory`, `muiloadsave`, `muiloadsavedelete`,
`muiloadfailed`, `muiloadsavemainresume` (palette, map tap moves the
player), `muiloadsavemainempty` - all finished, the test game's saves
identical afterwards. Android arm64 build installed on the phone.
