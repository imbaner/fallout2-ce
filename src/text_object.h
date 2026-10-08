#ifndef TEXT_OBJECT_H
#define TEXT_OBJECT_H

#include "color.h"
#include "geometry.h"
#include "obj_types.h"

namespace fallout {

int textObjectsInit(unsigned char* windowBuffer, int width, int height);
int textObjectsReset();
void textObjectsFree();
void textObjectsDisable();
void textObjectsEnable();
void textObjectsSetBaseDelay(double value);
void textObjectsSetLineDelay(double value);

int textObjectAdd(Object* object, char* string, int font, ColorWithFlags color, ColorWithFlags outlineColor, Rect* rect);

inline int textObjectAdd(Object* object, char* string, int font, Color color, Color outlineColor, Rect* rect)
{
    return textObjectAdd(object, string, font, color | DRAW_TEXT_FLAG_NONE, outlineColor | DRAW_TEXT_FLAG_NONE, rect);
}

void textObjectsRenderInRect(Rect* rect);
int textObjectsGetCount();
void textObjectsRemoveByOwner(Object* object);

// CE: The mobile UI draws the texts over the zoomed map at a fixed size
// (mui_floating_text.cc) instead of the game drawing them into it.
bool textObjectsDrawnOverMap();

struct TextObjectView {
    // In the game's encoding.
    const char* text;
    // Where it stays (where its object was when it was said).
    int tile;
    // Over the head of whoever said it, or at the tile (a text without an
    // object, put at the screen's center).
    bool aboveTile;
    ColorWithFlags color;
    ColorWithFlags outlineColor;
};

// The text object [index] (0..`textObjectsGetCount`).
bool textObjectGetView(int index, TextObjectView* view);

} // namespace fallout

#endif /* TEXT_OBJECT_H */
