#ifndef FALLOUT_SFALL_KB_HELPERS_H_
#define FALLOUT_SFALL_KB_HELPERS_H_

namespace fallout {

// Returns `true` if given key is pressed.
bool sfall_kb_is_key_pressed(int key);

// Simulates pressing `key`.
void sfall_kb_press_key(int key);

// CE: Presses hotkey [keys] (DirectInput codes, the first one is pressed,
// all of them count as held until it's released) as a keyboard would, for
// on-screen buttons of mods' hotkeys. Unlike `tap_key` the key press reaches
// `HOOK_KEYPRESS`.
void sfall_kb_press_hotkey(const int* keys, int count);

// Returns `true` when the next matching SDL key event was injected by `tap_key`.
bool sfall_kb_consume_synthetic_key_event(int sdlScanCode, bool pressed);

// CE: Returns `true` when the next matching SDL key event was pushed by
// `sfall_kb_press_hotkey` (unlike `tap_key` ones they reach `HOOK_KEYPRESS`).
bool sfall_kb_consume_hotkey_event(int sdlScanCode, bool pressed);

// Clears queued synthetic `tap_key` and hotkey markers after SDL key events
// are discarded.
void sfall_kb_clear_synthetic_key_events();

// Returns -1 when scripts did not override the key. Otherwise returns the
// replacement SDL scancode, or SDL_SCANCODE_UNKNOWN to swallow the event.
int sfall_kb_handle_key_pressed(int sdlScanCode, bool pressed);

} // namespace fallout

#endif /* FALLOUT_SFALL_KB_HELPERS_H_ */
