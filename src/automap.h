#ifndef AUTOMAP_H
#define AUTOMAP_H

#include <utility>
#include <vector>

#include "db.h"
#include "map_defs.h"
#include "worldmap.h"

namespace fallout {

#define AUTOMAP_DB ("AUTOMAP.DB")
#define AUTOMAP_TMP ("AUTOMAP.TMP")

// The number of map entries that is stored in automap.db.
//
// NOTE: I don't know why this value is not equal to the number of maps.
#define AUTOMAP_MAP_COUNT (160)

// View options for rendering automap for map window. These are stored in
// [gAutomapFlags] and is saved in save game file.
enum AutomapFlags : int {
    AUTOMAP_NONE = 0x00,

    // NOTE: This is a special flag to denote the map is activated in the game (as
    // opposed to the mapper). It's always on. Turning it off produces nice color
    // coded map with all objects and their types visible, however there is no way
    // you can do it within the game UI.
    AUTOMAP_IN_GAME = 0x01,

    // High details is on.
    AUTOMAP_WTH_HIGH_DETAILS = 0x02,

    // Scanner is active.
    AUTOMAP_WITH_SCANNER = 0x04,
};

constexpr inline AutomapFlags operator~(AutomapFlags rhs)
{
    return static_cast<AutomapFlags>(~static_cast<int>(rhs));
}

inline AutomapFlags& operator&=(AutomapFlags& lhs, AutomapFlags rhs)
{
    lhs = static_cast<AutomapFlags>(static_cast<int>(lhs) & static_cast<int>(rhs));
    return lhs;
}

inline AutomapFlags& operator|=(AutomapFlags& lhs, AutomapFlags rhs)
{
    lhs = static_cast<AutomapFlags>(static_cast<int>(lhs) | static_cast<int>(rhs));
    return lhs;
}

typedef struct AutomapHeader {
    unsigned char version;

    // The size of entire automap database (including header itself).
    int dataSize;

    // Offsets from the beginning of the automap database file into
    // entries data.
    //
    // These offsets are specified for every map/elevation combination. A value
    // of 0 specifies that there is no data for appropriate map/elevation
    // combination.
    int offsets[AUTOMAP_MAP_COUNT][ELEVATION_COUNT];
} AutomapHeader;

int automapInit();
int automapReset();
void automapExit();
int automapLoad(File* stream);
int automapSave(File* stream);
int _automapDisplayMap(int map);
void automapShow(bool isInGame, bool isUsingScanner);
int automapRenderInPipboyWindow(int win, Map map, int elevation);

// Explored walls (1) and scenery (2) of [map]'s [elevation] as saved for the
// Pip-Boy: HEX_GRID_WIDTH x HEX_GRID_HEIGHT tiles, rows top down, the way the
// Pip-Boy draws them. False when there's no data.
bool automapGetPipboyTiles(Map map, int elevation, std::vector<unsigned char>& tiles);

// CE: Mobile UI map screen (mui_automap.cc) over the automap's state.

// What the game's automap window shows of [elevation] now: walls and (with
// high details) scenery of objects seen as tiles like
// `automapGetPipboyTiles`, dude, exit grids, and critters with the motion
// sensor.
struct AutomapView {
    std::vector<unsigned char> tiles;
    int dudeX;
    int dudeY;
    std::vector<std::pair<int, int>> exits;
    std::vector<std::pair<int, int>> critters;
};

void automapGetView(int elevation, AutomapView* view);

bool automapGetHighDetails();
void automapSetHighDetails(bool highDetails);

// The motion sensor in dude's hands shows critters (uses a charge); returns
// the game's message when it can't (not installed, no charges).
const char* automapActivateScanner();
bool automapIsScannerActive();
int automapSaveCurrent();
int automapGetHeader(AutomapHeader** automapHeaderPtr);
int automapGetWindow();

void automapSetDisplayMap(Map map, bool available);

} // namespace fallout

#endif /* AUTOMAP_H */
