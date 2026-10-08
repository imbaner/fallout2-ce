#ifndef FALLOUT_HUD_LAYOUT_H_
#define FALLOUT_HUD_LAYOUT_H_

#include <vector>

namespace fallout {

// Resolution independent layout for the touch HUD (see touch_hud.h).
//
// HUD is made of independent groups, each attached to its own screen corner or
// edge. Sizes are in dp (Android density independent pixels), so controls
// keep the same physical size on any screen, and are converted to logical
// screen pixels with `HudMetrics`. Layouts are plain data tables, one per
// orientation; logic in touch_hud.cc only refers to elements by id.

enum class HudAnchor {
    TopLeft,
    Top,
    TopRight,
    Left,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
};

// Direction of lines in a group: `Rows` - each line is a row, lines are
// stacked top to bottom; `Columns` - each line is a column, lines go left to
// right.
enum class HudFlow {
    Rows,
    Columns,
};

// Alignment of shorter lines inside group.
enum class HudAlign {
    Start,
    Center,
    End,
};

enum class HudGroupId {
    System, // menu, quick save/load
    Screens, // inventory, character, Pip-Boy, map
    Tools, // skills, highlight, sneak
    Log, // message log button
    Combat, // end turn, end combat
    Weapon, // status, attack modes, swap hands, weapon
    Indicators, // SNEAK, LEVEL, POISONED, ...
    Count,
};

enum class HudElementId {
    Menu,
    QuickSave,
    QuickLoad,
    Inventory,
    Character,
    Pipboy,
    Map,
    Skills,
    Highlight,
    Sneak,
    Log,
    EndTurn,
    EndCombat,
    Status, // AP and HP
    Modes, // attack modes strip
    SwapHands,
    Weapon,
    Indicators,
    // Party Orders mod: list of its orders.
    PartyOrders,
    // Combat's tactical view (tactical_view.h).
    TacticalView,
    Count,
};

// Element size in dp. Width `kHudFill` stretches element to the longest line
// of its group.
constexpr float kHudFill = -1.0f;

struct HudElementSpec {
    HudElementId id;
    float width;
    float height;
};

struct HudGroupSpec {
    HudGroupId id;
    HudAnchor anchor;
    HudFlow flow;
    HudAlign align;
    std::vector<std::vector<HudElementSpec>> lines;
    // Group may move away from its anchor along the edge to avoid
    // overlapping groups placed before it.
    bool avoidOthers;
    // Space between the group edge and its elements, dp. Groups with
    // padding are drawn on a panel.
    float padding = 0.0f;
};

struct HudLayout {
    // Distance from screen (safe area) edges, dp.
    float margin;
    // Gap between elements and lines, dp.
    float gap;
    // Groups are placed in this order (`avoidOthers` groups go last).
    std::vector<HudGroupSpec> groups;
};

struct HudMetrics {
    // Logical screen size (game pixels).
    int screenWidth;
    int screenHeight;
    // Logical pixels per dp, including UI size setting.
    float pixelsPerDp;
    // Safe area insets (display cutouts), logical pixels.
    int insetLeft;
    int insetTop;
    int insetRight;
    int insetBottom;
};

struct HudRect {
    int x;
    int y;
    int width;
    int height;
};

struct HudPlacedElement {
    HudElementId id;
    // Relative to group rect.
    HudRect rect;
};

struct HudPlacedGroup {
    HudGroupId id;
    // Screen rect.
    HudRect rect;
    // Group is drawn on a panel.
    bool framed;
    std::vector<HudPlacedElement> elements;
};

HudMetrics hudMetricsGet();
// Logical pixels per dp (UI size setting included).
float hudGetPixelsPerDp();

// Screen density (Android dp scale), UI size setting not included.
float hudGetScreenDensity();
int hudDpToPixels(const HudMetrics& metrics, float dp);

const HudLayout& hudLayoutLandscape();

// Computes screen rects of all groups and their elements.
std::vector<HudPlacedGroup> hudLayoutPlace(const HudLayout& layout, const HudMetrics& metrics);

} // namespace fallout

#endif /* FALLOUT_HUD_LAYOUT_H_ */
