#ifndef FALLOUT_MUI_MAP_VIEW_H_
#define FALLOUT_MUI_MAP_VIEW_H_

#include <string>
#include <vector>

#include "mui.h"

namespace fallout {

// Explored map of the mobile UI (Pip-Boy maps, automap): walls as glowing
// lines, scenery as dim dots, markers on top; one finger drags it, two
// fingers pinch it, like the world view. The game's data is drawn as it is
// (the tiles it keeps for the automap), only the look is new.

// Tile codes of the map.
enum MuiMapTile : unsigned char {
    MUI_MAP_TILE_EMPTY = 0,
    MUI_MAP_TILE_WALL = 1,
    MUI_MAP_TILE_SCENERY = 2,
};

// Marker in tile coordinates (tile centers are at x + 0.5, y + 0.5).
struct MuiMapMarker {
    float x;
    float y;
    MuiColor color;
    // dp.
    float radius;
};

class MuiMapView {
public:
    // [tiles]: [width] x [height] codes, rows top down. A new [key] (another
    // map) shows the explored part whole; the same key keeps the view.
    void setTiles(const std::string& key, const std::vector<unsigned char>& tiles, int width, int height);

    bool isEmpty() const { return right < left; }

    // Shows the explored part whole.
    void fit() { needsFit = true; }

    // Centers the view on tile ([x], [y]) (e.g. the player).
    void centerOn(float x, float y);

    void build(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<MuiMapMarker>& markers);

private:
    std::string key;
    std::vector<unsigned char> tiles;
    int width = 0;
    int height = 0;

    // Explored part (tiles), with a margin.
    int left = 0;
    int top = 0;
    int right = -1;
    int bottom = -1;

    MuiPanZoom view;
    bool needsFit = true;
    bool hasCenter = false;
    float centerX = 0.0f;
    float centerY = 0.0f;

    // The drawn map (texture pixels per tile, drawn again at another scale
    // after zooming).
    SDL_Texture* texture = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    float textureScale = 0.0f;
    unsigned int textureGeneration = 0;
    bool textureValid = false;

    unsigned char tileAt(int x, int y) const;
    void render(MuiContext& ui, float scale);
};

} // namespace fallout

#endif /* FALLOUT_MUI_MAP_VIEW_H_ */
