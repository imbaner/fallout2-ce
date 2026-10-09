#ifndef FALLOUT_GROUND_LINES_H_
#define FALLOUT_GROUND_LINES_H_

#include <SDL.h>

#include <vector>

#include "mui_draw.h"

namespace fallout {

// CE: Lines lying on the map's ground (the tactical view's tiles), drawn by
// the mobile UI over the map: smooth, as thick at any zoom, but where
// something stands in front of them they're dimmed or hidden by what the
// game drew there (`objectSeeThroughCover`): faint behind scenery, half
// through see-through critters, none under the dude and the outlines.
//
// Paths are batched between `groundLinesBegin` and `groundLinesEnd` (one
// draw, in the order added). Points are in the game's buffer (world)
// coordinates; widths in output pixels.

void groundLinesBegin(float scale);
// One stroke along [points]: joined corners, soft edges.
void groundLinesAddPath(const std::vector<SDL_FPoint>& points, bool closed, float width, MuiColor color);
void groundLinesEnd();

} // namespace fallout

#endif /* FALLOUT_GROUND_LINES_H_ */
