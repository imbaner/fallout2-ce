#ifndef FALLOUT_WORLD_TEXTURE_H_
#define FALLOUT_WORLD_TEXTURE_H_

#include <SDL.h>

#include "geometry.h"

namespace fallout {

// CE: GPU copy of the world buffer (8-bit, game palette) drawn by the world
// view (world_view.h).
//
// - Tiles: the copy is made of 256x256 textures. Updating a texture the
//   previous frame still draws makes mobile drivers copy or wait for the
//   whole texture, so small textures keep that cheap, and each one is
//   updated at most once a frame (all its changed areas together).
// - Ring addressing: the game scrolls the map by moving the buffer contents
//   and drawing only the uncovered strips (`mapScrollImmediate`); the copy
//   isn't moved, only its origin, so a scroll uploads just the strips.
// - Palette: pixels are converted with the palette when uploaded. The game
//   cycles a few entries many times a second (water, fire, monitors, alarm
//   lights); each tile remembers which of them it uses and where, and only
//   tiles using entries which really changed are uploaded again.

bool worldTextureInit(unsigned char* buffer, int width, int height);
void worldTextureFree();

// Textures belong to the renderer; they're made again on the next draw.
void worldTextureResetRenderer();

// Area of the world buffer changed (buffer coordinates).
void worldTextureInvalidate(const Rect* rect);
void worldTextureInvalidateAll();

// The game's palette changed (which entries is found on the next upload).
void worldTexturePaletteChanged();

// The buffer contents moved by (-dx, -dy) (map scrolled by (dx, dy)); the
// uncovered strips are invalidated by the game as it draws them.
void worldTextureScrolled(int dx, int dy);

// Uploads changes and draws [source] (buffer coordinates) scaled by the
// integer [factor] into the current render target at its top left corner.
// Returns false if nothing could be drawn.
bool worldTextureUpload(SDL_Renderer* renderer);
void worldTextureDraw(SDL_Renderer* renderer, const SDL_Rect& source, int factor);

// Frame counter of the last change drawn (uploads or origin moves), so a
// composed image of the same source can be reused while it's the same.
unsigned int worldTextureVersion();

} // namespace fallout

#endif /* FALLOUT_WORLD_TEXTURE_H_ */
