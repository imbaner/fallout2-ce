#include "text_object.h"

#include <algorithm>
#include <string.h>

#include "color.h"
#include "debug.h"
#include "draw.h"
#include "input.h"
#include "memory.h"
#include "mui.h"
#include "object.h"
#include "settings.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "word_wrap.h"
#include "world_view.h"

namespace fallout {

// The maximum number of text objects that can exist at the same time.
#define TEXT_OBJECTS_MAX_COUNT (200)

typedef enum TextObjectFlags {
    TEXT_OBJECT_MARKED_FOR_REMOVAL = 0x01,
    TEXT_OBJECT_UNBOUNDED = 0x02,
} TextObjectFlags;

typedef struct TextObject {
    int flags;
    Object* owner;
    unsigned int time;
    int linesCount;
    int sx;
    int sy;
    int tile;
    int x;
    int y;
    int width;
    int height;
    unsigned char* data;
    // CE: The text and its colors for the mobile UI's overlay (see
    // `textObjectsDrawnOverMap`).
    char* text;
    ColorWithFlags color;
    ColorWithFlags outlineColor;
} TextObject;

static void textObjectsTicker();
static void textObjectFindPlacement(TextObject* textObject);
static void textObjectFree(TextObject* textObject);

// 0x51D944 text_object_index
static int gTextObjectsCount = 0;

// 0x51D948 text_object_base_delay
static unsigned int gTextObjectsBaseDelay = 3500;

// 0x51D94C text_object_line_delay
static unsigned int gTextObjectsLineDelay = 1399;

// 0x6681C0 text_object_list
static TextObject* gTextObjects[TEXT_OBJECTS_MAX_COUNT];

// 0x668210 display_width
static int gTextObjectsWindowWidth;

// 0x668214 display_height
static int gTextObjectsWindowHeight;

// 0x668218 display_buffer
static unsigned char* gTextObjectsWindowBuffer;

// 0x66821C text_object_enabled
static bool gTextObjectsEnabled;

// 0x668220 text_object_initialized
static bool gTextObjectsInitialized;

// 0x4B0130 text_object_init
int textObjectsInit(unsigned char* windowBuffer, int width, int height)
{
    if (gTextObjectsInitialized) {
        return -1;
    }

    gTextObjectsWindowBuffer = windowBuffer;
    gTextObjectsWindowWidth = width;
    gTextObjectsWindowHeight = height;
    gTextObjectsCount = 0;

    tickersAdd(textObjectsTicker);

    gTextObjectsBaseDelay = (unsigned int)(settings.preferences.text_base_delay * 1000.0);
    gTextObjectsLineDelay = (unsigned int)(settings.preferences.text_line_delay * 1000.0);

    gTextObjectsEnabled = true;
    gTextObjectsInitialized = true;

    return 0;
}

// 0x4B021C text_object_reset
int textObjectsReset()
{
    if (!gTextObjectsInitialized) {
        return -1;
    }

    for (int index = 0; index < gTextObjectsCount; index++) {
        textObjectFree(gTextObjects[index]);
    }

    gTextObjectsCount = 0;
    tickersAdd(textObjectsTicker);

    return 0;
}

// 0x4B0280 text_object_exit
void textObjectsFree()
{
    if (gTextObjectsInitialized) {
        textObjectsReset();
        tickersRemove(textObjectsTicker);
        gTextObjectsInitialized = false;
    }
}

// 0x4B02A4 text_object_disable
void textObjectsDisable()
{
    gTextObjectsEnabled = false;
}

// 0x4B02B0 text_object_enable
void textObjectsEnable()
{
    gTextObjectsEnabled = true;
}

// 0x4B02C4 text_object_set_base_delay
void textObjectsSetBaseDelay(double value)
{
    if (value < 1.0) {
        value = 1.0;
    }

    gTextObjectsBaseDelay = (int)(value * 1000.0);
}

// 0x4B031C text_object_set_line_delay
void textObjectsSetLineDelay(double value)
{
    if (value < 0.0) {
        value = 0.0;
    }

    gTextObjectsLineDelay = (int)(value * 1000.0);
}

// text_object_create
// 0x4B036C text_object_create
int textObjectAdd(Object* object, char* string, int font, ColorWithFlags color, ColorWithFlags outlineColor, Rect* rect)
{
    if (!gTextObjectsInitialized) {
        return -1;
    }

    // SFALL: Fix incorrect value of the limit number of floating messages.
    if (gTextObjectsCount >= TEXT_OBJECTS_MAX_COUNT) {
        return -1;
    }

    if (string == nullptr) {
        return -1;
    }

    if (*string == '\0') {
        return -1;
    }

    TextObject* textObject = (TextObject*)internal_malloc(sizeof(*textObject));
    if (textObject == nullptr) {
        return -1;
    }

    memset(textObject, 0, sizeof(*textObject));

    textObject->text = internal_strdup(string);
    textObject->color = color;
    textObject->outlineColor = outlineColor;

    int oldFont = fontGetCurrent();
    fontSetCurrent(font);

    short beginnings[WORD_WRAP_MAX_COUNT];
    short count;
    if (wordWrap(string, 200, beginnings, &count) != 0) {
        fontSetCurrent(oldFont);
        textObjectFree(textObject);
        return -1;
    }

    textObject->linesCount = count - 1;
    if (textObject->linesCount < 1) {
        debugPrint("**Error in text_object_create()\n");
    }

    textObject->width = 0;

    for (int index = 0; index < textObject->linesCount; index++) {
        char* ending = string + beginnings[index + 1];
        char* beginning = string + beginnings[index];
        if (ending[-1] == ' ') {
            --ending;
        }

        char c = *ending;
        *ending = '\0';

        // NOTE: Calls `fontGetStringWidth` twice.
        textObject->width = std::max(textObject->width, fontGetStringWidth(beginning));

        *ending = c;
    }

    textObject->height = (fontGetLineHeight() + 1) * textObject->linesCount;

    if (outlineColor != COLOR_INVALID) {
        textObject->width += 2;
        textObject->height += 2;
    }

    int size = textObject->width * textObject->height;
    textObject->data = (unsigned char*)internal_malloc(size);
    if (textObject->data == nullptr) {
        fontSetCurrent(oldFont);
        textObjectFree(textObject);
        return -1;
    }

    memset(textObject->data, 0, size);

    unsigned char* dest = textObject->data;
    int skip = textObject->width * (fontGetLineHeight() + 1);

    if (outlineColor != COLOR_INVALID) {
        dest += textObject->width;
    }

    for (int index = 0; index < textObject->linesCount; index++) {
        char* beginning = string + beginnings[index];
        char* ending = string + beginnings[index + 1];
        if (ending[-1] == ' ') {
            --ending;
        }

        char c = *ending;
        *ending = '\0';

        int width = fontGetStringWidth(beginning);
        fontDrawText(dest + (textObject->width - width) / 2, beginning, textObject->width, textObject->width, color);

        *ending = c;

        dest += skip;
    }

    if (outlineColor != COLOR_INVALID) {
        bufferOutline(textObject->data, textObject->width, textObject->height, textObject->width, static_cast<Color>(outlineColor & COLOR_LAST));
    }

    if (object != nullptr) {
        textObject->tile = object->tile;
    } else {
        textObject->flags |= TEXT_OBJECT_UNBOUNDED;
        textObject->tile = gCenterTile;
    }

    textObjectFindPlacement(textObject);

    if (rect != nullptr) {
        rect->left = textObject->x;
        rect->top = textObject->y;
        rect->right = textObject->x + textObject->width - 1;
        rect->bottom = textObject->y + textObject->height - 1;
    }

    textObjectsRemoveByOwner(object);

    textObject->owner = object;
    textObject->time = _get_bk_time();

    gTextObjects[gTextObjectsCount] = textObject;
    gTextObjectsCount++;

    fontSetCurrent(oldFont);

    return 0;
}

// 0x4B06E8 text_object_render
void textObjectsRenderInRect(Rect* rect)
{
    // CE: The mobile UI draws them over the map instead.
    if (!gTextObjectsInitialized || textObjectsDrawnOverMap()) {
        return;
    }

    for (int index = 0; index < gTextObjectsCount; index++) {
        TextObject* textObject = gTextObjects[index];
        tileToScreenXY(textObject->tile, &(textObject->x), &(textObject->y));
        textObject->x += textObject->sx;
        textObject->y += textObject->sy;

        Rect textObjectRect;
        textObjectRect.left = textObject->x;
        textObjectRect.top = textObject->y;
        textObjectRect.right = textObject->width + textObject->x - 1;
        textObjectRect.bottom = textObject->height + textObject->y - 1;
        if (rectIntersection(&textObjectRect, rect, &textObjectRect) == 0) {
            blitBufferToBufferTrans(textObject->data + textObject->width * (textObjectRect.top - textObject->y) + (textObjectRect.left - textObject->x),
                textObjectRect.right - textObjectRect.left + 1,
                textObjectRect.bottom - textObjectRect.top + 1,
                textObject->width,
                gTextObjectsWindowBuffer + gTextObjectsWindowWidth * textObjectRect.top + textObjectRect.left,
                gTextObjectsWindowWidth);
        }
    }
}

// 0x4B07F0 text_object_count
int textObjectsGetCount()
{
    return gTextObjectsCount;
}

// 0x4B07F8 text_object_bk
static void textObjectsTicker()
{
    if (!gTextObjectsEnabled) {
        return;
    }

    bool textObjectsRemoved = false;
    Rect dirtyRect;

    for (int index = 0; index < gTextObjectsCount; index++) {
        TextObject* textObject = gTextObjects[index];

        unsigned int delay = gTextObjectsLineDelay * textObject->linesCount + gTextObjectsBaseDelay;
        if ((textObject->flags & TEXT_OBJECT_MARKED_FOR_REMOVAL) != 0 || (getTicksBetween(_get_bk_time(), textObject->time) >= delay)) {
            tileToScreenXY(textObject->tile, &(textObject->x), &(textObject->y));
            textObject->x += textObject->sx;
            textObject->y += textObject->sy;

            Rect textObjectRect;
            textObjectRect.left = textObject->x;
            textObjectRect.top = textObject->y;
            textObjectRect.right = textObject->width + textObject->x - 1;
            textObjectRect.bottom = textObject->height + textObject->y - 1;

            if (textObjectsRemoved) {
                rectUnion(&dirtyRect, &textObjectRect, &dirtyRect);
            } else {
                rectCopy(&dirtyRect, &textObjectRect);
                textObjectsRemoved = true;
            }

            textObjectFree(textObject);

            memmove(&(gTextObjects[index]), &(gTextObjects[index + 1]), sizeof(*gTextObjects) * (gTextObjectsCount - index - 1));

            gTextObjectsCount--;
            index--;
        }
    }

    if (textObjectsRemoved) {
        tileWindowRefreshRect(&dirtyRect, gElevation);
    }
}

// Finds best position for placing text object.
//
// 0x4B0954 text_object_get_offset
static void textObjectFindPlacement(TextObject* textObject)
{
    // CE: With the zoomable world view the buffer is bigger than what is on
    // screen, keep texts in its visible part.
    Rect bounds;
    worldViewGetVisibleRect(&bounds);
    bounds.left = std::max(bounds.left, 0);
    bounds.top = std::max(bounds.top, 0);
    bounds.right = std::min(bounds.right, gTextObjectsWindowWidth - 1);
    bounds.bottom = std::min(bounds.bottom, gTextObjectsWindowHeight - 1);

    int tileScreenX;
    int tileScreenY;
    tileToScreenXY(textObject->tile, &tileScreenX, &tileScreenY);

    auto fits = [&]() {
        return textObject->x >= bounds.left && textObject->x + textObject->width - 1 <= bounds.right
            && textObject->y >= bounds.top && textObject->y + textObject->height - 1 <= bounds.bottom;
    };

    auto place = [&]() {
        textObject->sx = textObject->x - tileScreenX;
        textObject->sy = textObject->y - tileScreenY;
    };

    textObject->x = tileScreenX + 16 - textObject->width / 2;
    textObject->y = tileScreenY;

    if ((textObject->flags & TEXT_OBJECT_UNBOUNDED) == 0) {
        textObject->y -= textObject->height + 60;
    }

    if (fits()) {
        place();
        return;
    }

    textObject->x -= textObject->width / 2;
    if (fits()) {
        place();
        return;
    }

    textObject->x += textObject->width;
    if (fits()) {
        place();
        return;
    }

    textObject->x = tileScreenX - 16 - textObject->width;
    textObject->y = tileScreenY - 16 - textObject->height;
    if (fits()) {
        place();
        return;
    }

    textObject->x += textObject->width + 64;
    if (fits()) {
        place();
        return;
    }

    textObject->x = tileScreenX + 16 - textObject->width / 2;
    textObject->y = tileScreenY;
    if (fits()) {
        place();
        return;
    }

    textObject->x -= textObject->width / 2;
    if (fits()) {
        place();
        return;
    }

    textObject->x += textObject->width;
    if (fits()) {
        place();
        return;
    }

    textObject->x = tileScreenX + 16 - textObject->width / 2;
    textObject->y = tileScreenY - (textObject->height + 60);
    place();
}

static void textObjectFree(TextObject* textObject)
{
    if (textObject->text != nullptr) {
        internal_free(textObject->text);
    }
    if (textObject->data != nullptr) {
        internal_free(textObject->data);
    }
    internal_free(textObject);
}

bool textObjectsDrawnOverMap()
{
    return muiIsEnabled() && worldViewIsEnabled();
}

bool textObjectGetView(int index, TextObjectView* view)
{
    if (index < 0 || index >= gTextObjectsCount) {
        return false;
    }

    TextObject* textObject = gTextObjects[index];
    view->text = textObject->text;
    view->tile = textObject->tile;
    view->aboveTile = (textObject->flags & TEXT_OBJECT_UNBOUNDED) == 0;
    view->color = textObject->color;
    view->outlineColor = textObject->outlineColor;
    return textObject->text != nullptr;
}

// Marks text objects attached to [object] for removal.
//
// 0x4B0C00 text_object_remove
void textObjectsRemoveByOwner(Object* object)
{
    for (int index = 0; index < gTextObjectsCount; index++) {
        if (gTextObjects[index]->owner == object) {
            gTextObjects[index]->flags |= TEXT_OBJECT_MARKED_FOR_REMOVAL;
        }
    }
}

} // namespace fallout
