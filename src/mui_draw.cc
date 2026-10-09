#include "mui_draw.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>

#include <string.h>

#include "art.h"
#include "color.h"
#include "db.h"
#include "debug.h"
#include "mui_icons.h"
#include "platform_compat.h"
#include "settings.h"
#include "svga.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace fallout {

namespace {

    // Font shipped in ce.dat (PT Mono, OFL, see fonts/OFL.txt).
    constexpr const char* kFontPath = "fonts\\PTM55FT.ttf";

    // Glyph ranges packed into atlases: ASCII, Latin-1, Latin Extended-A,
    // Cyrillic, general punctuation, currency and letterlike symbols.
    struct GlyphRange {
        int first;
        int count;
    };

    constexpr GlyphRange kGlyphRanges[] = {
        { 0x20, 0x7F - 0x20 },
        { 0xA0, 0x180 - 0xA0 },
        { 0x400, 0x460 - 0x400 },
        { 0x490, 2 },
        { 0x2010, 0x2027 - 0x2010 + 1 },
        { 0x2030, 0x203A - 0x2030 + 1 },
        { 0x20AC, 1 },
        { 0x2116, 1 },
        { 0x2122, 1 },
        { 0x2212, 1 },
    };

    constexpr int kRangeCount = sizeof(kGlyphRanges) / sizeof(kGlyphRanges[0]);

    struct FontAtlas {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        std::vector<stbtt_packedchar> chars[kRangeCount];
        float ascent = 0.0f;
        float lineHeight = 0.0f;
    };

    SDL_Renderer* gRenderer = nullptr;

    std::vector<unsigned char> gFontData;
    stbtt_fontinfo gFontInfo;
    bool gFontLoaded = false;
    bool gFontLoadFailed = false;

    // By pixel size.
    std::map<int, std::unique_ptr<FontAtlas>> gAtlases;

    std::vector<SDL_Rect> gClipStack;

    struct ArtTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        unsigned int paletteVersion = 0;
        // The art isn't there (not looked up again).
        bool missing = false;
    };

    // By fid and frame.
    std::map<std::pair<int, int>, ArtTexture> gArtTextures;

    // Painted pictures by fid.
    // Named FRMs all have the same empty FID. Own the path in the key and
    // include its object type (relative interface path vs full asset path).
    std::map<std::tuple<int, int, std::string>, ArtTexture> gPictureTextures;

    // Line drawings (skilldex pictures) by fid.
    std::map<int, ArtTexture> gLineArtTextures;

    struct IndexedTexture {
        ArtTexture base;
        unsigned int version = 0;
    };
    std::map<std::string, IndexedTexture> gIndexedTextures;
    unsigned char gPalette[768];
    unsigned int gPaletteVersion = 0;
    unsigned int gPaletteCheckFrame = 0;
    unsigned int gFrame = 0;

    std::vector<SDL_Vertex> gVertices;

    bool loadFont()
    {
        if (gFontLoaded) {
            return true;
        }

        if (gFontLoadFailed) {
            return false;
        }

        File* stream = fileOpen(kFontPath, "rb");
        if (stream == nullptr) {
            debugPrint("MUI: can't open font %s\n", kFontPath);
            gFontLoadFailed = true;
            return false;
        }

        int size = fileGetSize(stream);
        gFontData.resize(size);
        size_t read = fileRead(gFontData.data(), 1, size, stream);
        fileClose(stream);

        if (static_cast<int>(read) != size || !stbtt_InitFont(&gFontInfo, gFontData.data(), stbtt_GetFontOffsetForIndex(gFontData.data(), 0))) {
            debugPrint("MUI: can't read font %s\n", kFontPath);
            gFontLoadFailed = true;
            return false;
        }

        gFontLoaded = true;
        return true;
    }

    FontAtlas* atlasForSize(float size)
    {
        int pixelSize = std::max(static_cast<int>(std::lround(size)), 6);
        auto it = gAtlases.find(pixelSize);
        if (it != gAtlases.end()) {
            return it->second.get();
        }

        if (gRenderer == nullptr || !loadFont()) {
            return nullptr;
        }

        auto atlas = std::make_unique<FontAtlas>();

        // Grow atlas until all glyphs fit.
        std::vector<unsigned char> pixels;
        for (int dimension = 512; dimension <= 4096; dimension *= 2) {
            pixels.assign(static_cast<size_t>(dimension) * dimension, 0);

            stbtt_pack_context context;
            if (!stbtt_PackBegin(&context, pixels.data(), dimension, dimension, 0, 1, nullptr)) {
                return nullptr;
            }

            stbtt_PackSetOversampling(&context, 2, 1);

            stbtt_pack_range ranges[kRangeCount];
            for (int index = 0; index < kRangeCount; index++) {
                atlas->chars[index].assign(kGlyphRanges[index].count, stbtt_packedchar {});
                ranges[index].font_size = static_cast<float>(pixelSize);
                ranges[index].first_unicode_codepoint_in_range = kGlyphRanges[index].first;
                ranges[index].array_of_unicode_codepoints = nullptr;
                ranges[index].num_chars = kGlyphRanges[index].count;
                ranges[index].chardata_for_range = atlas->chars[index].data();
            }

            bool packed = stbtt_PackFontRanges(&context, gFontData.data(), 0, ranges, kRangeCount) != 0;
            stbtt_PackEnd(&context);

            if (packed) {
                atlas->width = dimension;
                atlas->height = dimension;
                break;
            }
        }

        if (atlas->width == 0) {
            return nullptr;
        }

        // White glyphs with coverage in alpha, colored by vertex color.
        std::vector<Uint32> argb(pixels.size());
        for (size_t index = 0; index < pixels.size(); index++) {
            argb[index] = (static_cast<Uint32>(pixels[index]) << 24) | 0x00FFFFFF;
        }

        atlas->texture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, atlas->width, atlas->height);
        if (atlas->texture == nullptr) {
            return nullptr;
        }

        SDL_UpdateTexture(atlas->texture, nullptr, argb.data(), atlas->width * 4);
        SDL_SetTextureBlendMode(atlas->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(atlas->texture, SDL_ScaleModeLinear);

        float scale = stbtt_ScaleForPixelHeight(&gFontInfo, static_cast<float>(pixelSize));
        int ascent;
        int descent;
        int lineGap;
        stbtt_GetFontVMetrics(&gFontInfo, &ascent, &descent, &lineGap);
        atlas->ascent = ascent * scale;
        atlas->lineHeight = (ascent - descent + lineGap) * scale;

        FontAtlas* result = atlas.get();
        gAtlases[pixelSize] = std::move(atlas);
        return result;
    }

    const stbtt_packedchar* findGlyph(FontAtlas* atlas, char32_t codepoint, int* rangeIndex, int* charIndex)
    {
        for (int index = 0; index < kRangeCount; index++) {
            int offset = static_cast<int>(codepoint) - kGlyphRanges[index].first;
            if (offset >= 0 && offset < kGlyphRanges[index].count) {
                *rangeIndex = index;
                *charIndex = offset;
                return &(atlas->chars[index][offset]);
            }
        }
        return nullptr;
    }

    SDL_Vertex vertex(float x, float y, MuiColor color, float u = 0.0f, float v = 0.0f)
    {
        SDL_Vertex result;
        result.position.x = x;
        result.position.y = y;
        result.color = { color.r, color.g, color.b, color.a };
        result.tex_coord.x = u;
        result.tex_coord.y = v;
        return result;
    }

    // Outline of a rounded rect, clockwise.
    int roundRectSegments(float radius)
    {
        return std::clamp(static_cast<int>(radius / 2.0f), 3, 12);
    }

    void roundRectOutline(const MuiRect& rect, float radius, std::vector<SDL_FPoint>& points, int segments)
    {
        radius = std::max(std::min(radius, std::min(rect.w, rect.h) / 2.0f), 0.0f);

        struct Corner {
            float cx;
            float cy;
            float start;
        };

        const float pi = 3.14159265f;
        Corner corners[4] = {
            { rect.x + rect.w - radius, rect.y + radius, -pi / 2.0f },
            { rect.x + rect.w - radius, rect.y + rect.h - radius, 0.0f },
            { rect.x + radius, rect.y + rect.h - radius, pi / 2.0f },
            { rect.x + radius, rect.y + radius, pi },
        };

        points.clear();
        for (const Corner& corner : corners) {
            for (int step = 0; step <= segments; step++) {
                float angle = corner.start + (pi / 2.0f) * step / segments;
                points.push_back({ corner.cx + std::cos(angle) * radius, corner.cy + std::sin(angle) * radius });
            }
        }
    }

    void fillPolygon(const std::vector<SDL_FPoint>& points, float centerX, float centerY, MuiColor color)
    {
        gVertices.clear();
        size_t count = points.size();
        for (size_t index = 0; index < count; index++) {
            const SDL_FPoint& a = points[index];
            const SDL_FPoint& b = points[(index + 1) % count];
            gVertices.push_back(vertex(centerX, centerY, color));
            gVertices.push_back(vertex(a.x, a.y, color));
            gVertices.push_back(vertex(b.x, b.y, color));
        }
        SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
    }

    void strokePolygons(const std::vector<SDL_FPoint>& outer, const std::vector<SDL_FPoint>& inner, MuiColor color)
    {
        gVertices.clear();
        size_t count = std::min(outer.size(), inner.size());
        for (size_t index = 0; index < count; index++) {
            size_t next = (index + 1) % count;
            gVertices.push_back(vertex(outer[index].x, outer[index].y, color));
            gVertices.push_back(vertex(outer[next].x, outer[next].y, color));
            gVertices.push_back(vertex(inner[index].x, inner[index].y, color));
            gVertices.push_back(vertex(inner[index].x, inner[index].y, color));
            gVertices.push_back(vertex(outer[next].x, outer[next].y, color));
            gVertices.push_back(vertex(inner[next].x, inner[next].y, color));
        }
        SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
    }

    void circleOutline(float centerX, float centerY, float radius, std::vector<SDL_FPoint>& points)
    {
        int segments = std::clamp(static_cast<int>(radius), 16, 96);
        points.clear();
        for (int step = 0; step < segments; step++) {
            float angle = 2.0f * 3.14159265f * step / segments;
            points.push_back({ centerX + std::cos(angle) * radius, centerY + std::sin(angle) * radius });
        }
    }

    // Upper halves (0x80-0xFF) of Windows code pages used by game texts.
    const char16_t kCp1251[128] = {
        0x0402,
        0x0403,
        0x201A,
        0x0453,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0x20AC,
        0x2030,
        0x0409,
        0x2039,
        0x040A,
        0x040C,
        0x040B,
        0x040F,
        0x0452,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0xFFFD,
        0x2122,
        0x0459,
        0x203A,
        0x045A,
        0x045C,
        0x045B,
        0x045F,
        0x00A0,
        0x040E,
        0x045E,
        0x0408,
        0x00A4,
        0x0490,
        0x00A6,
        0x00A7,
        0x0401,
        0x00A9,
        0x0404,
        0x00AB,
        0x00AC,
        0x00AD,
        0x00AE,
        0x0407,
        0x00B0,
        0x00B1,
        0x0406,
        0x0456,
        0x0491,
        0x00B5,
        0x00B6,
        0x00B7,
        0x0451,
        0x2116,
        0x0454,
        0x00BB,
        0x0458,
        0x0405,
        0x0455,
        0x0457,
        0x0410,
        0x0411,
        0x0412,
        0x0413,
        0x0414,
        0x0415,
        0x0416,
        0x0417,
        0x0418,
        0x0419,
        0x041A,
        0x041B,
        0x041C,
        0x041D,
        0x041E,
        0x041F,
        0x0420,
        0x0421,
        0x0422,
        0x0423,
        0x0424,
        0x0425,
        0x0426,
        0x0427,
        0x0428,
        0x0429,
        0x042A,
        0x042B,
        0x042C,
        0x042D,
        0x042E,
        0x042F,
        0x0430,
        0x0431,
        0x0432,
        0x0433,
        0x0434,
        0x0435,
        0x0436,
        0x0437,
        0x0438,
        0x0439,
        0x043A,
        0x043B,
        0x043C,
        0x043D,
        0x043E,
        0x043F,
        0x0440,
        0x0441,
        0x0442,
        0x0443,
        0x0444,
        0x0445,
        0x0446,
        0x0447,
        0x0448,
        0x0449,
        0x044A,
        0x044B,
        0x044C,
        0x044D,
        0x044E,
        0x044F,
    };

    const char16_t kCp1252Low[32] = {
        0x20AC,
        0xFFFD,
        0x201A,
        0x0192,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0x02C6,
        0x2030,
        0x0160,
        0x2039,
        0x0152,
        0xFFFD,
        0x017D,
        0xFFFD,
        0xFFFD,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0x02DC,
        0x2122,
        0x0161,
        0x203A,
        0x0153,
        0xFFFD,
        0x017E,
        0x0178,
    };

    bool gameTextIsCp1251()
    {
        const std::string& language = settings.system.language;
        return compat_stricmp(language.c_str(), "russian") == 0 || compat_stricmp(language.c_str(), "ukrainian") == 0;
    }

} // namespace

void muiDrawBegin(SDL_Renderer* renderer)
{
    gRenderer = renderer;
    gClipStack.clear();
    gFrame++;
    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
}

void muiDrawEnd()
{
    if (gRenderer != nullptr) {
        SDL_RenderSetClipRect(gRenderer, nullptr);
    }
    gClipStack.clear();
}

static unsigned int gDrawGeneration = 1;

unsigned int muiDrawGeneration()
{
    return gDrawGeneration;
}

SDL_BlendMode muiPremultipliedBlendMode()
{
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}

void muiDrawReset()
{
    gDrawGeneration++;

    for (auto& entry : gIndexedTextures) {
        if (entry.second.base.texture != nullptr) {
            SDL_DestroyTexture(entry.second.base.texture);
        }
    }
    gIndexedTextures.clear();

    for (auto& entry : gArtTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gArtTextures.clear();

    for (auto& entry : gPictureTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gPictureTextures.clear();

    for (auto& entry : gLineArtTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gLineArtTextures.clear();

    for (auto& entry : gAtlases) {
        if (entry.second->texture != nullptr) {
            SDL_DestroyTexture(entry.second->texture);
        }
    }
    gAtlases.clear();
    muiIconsReset();
    gRenderer = nullptr;
}

SDL_Renderer* muiDrawGetRenderer()
{
    return gRenderer;
}

void muiFillRect(const MuiRect& rect, MuiColor color)
{
    SDL_FRect frect = { rect.x, rect.y, rect.w, rect.h };
    SDL_SetRenderDrawColor(gRenderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRectF(gRenderer, &frect);
}

void muiFillRoundRect(const MuiRect& rect, float radius, MuiColor color)
{
    if (radius < 1.0f) {
        muiFillRect(rect, color);
        return;
    }

    std::vector<SDL_FPoint> points;
    roundRectOutline(rect, radius, points, roundRectSegments(radius));
    fillPolygon(points, rect.centerX(), rect.centerY(), color);
}

void muiStrokeRoundRect(const MuiRect& rect, float radius, float thickness, MuiColor color)
{
    // Same segment count, so outlines have the same point count.
    int segments = roundRectSegments(radius);
    std::vector<SDL_FPoint> outer;
    std::vector<SDL_FPoint> inner;
    roundRectOutline(rect, radius, outer, segments);
    roundRectOutline(rect.inset(thickness), std::max(radius - thickness, 0.0f), inner, segments);
    strokePolygons(outer, inner, color);
}

void muiFillCircle(float centerX, float centerY, float radius, MuiColor color)
{
    std::vector<SDL_FPoint> points;
    circleOutline(centerX, centerY, radius, points);
    fillPolygon(points, centerX, centerY, color);
}

void muiStrokeCircle(float centerX, float centerY, float radius, float thickness, MuiColor color)
{
    std::vector<SDL_FPoint> outer;
    std::vector<SDL_FPoint> inner;
    circleOutline(centerX, centerY, radius, outer);
    circleOutline(centerX, centerY, radius, inner);
    for (size_t index = 0; index < inner.size(); index++) {
        inner[index].x = centerX + (inner[index].x - centerX) * (radius - thickness) / radius;
        inner[index].y = centerY + (inner[index].y - centerY) * (radius - thickness) / radius;
    }
    strokePolygons(outer, inner, color);
}

void muiFillHorizontalGradient(const MuiRect& rect, MuiColor left, MuiColor right)
{
    muiFillGradient(rect, left, right, right, left);
}

void muiFillGradient(const MuiRect& rect, MuiColor topLeft, MuiColor topRight, MuiColor bottomRight, MuiColor bottomLeft)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    gVertices.clear();
    gVertices.push_back(vertex(rect.x, rect.y, topLeft));
    gVertices.push_back(vertex(rect.right(), rect.y, topRight));
    gVertices.push_back(vertex(rect.right(), rect.bottom(), bottomRight));
    gVertices.push_back(vertex(rect.x, rect.y, topLeft));
    gVertices.push_back(vertex(rect.right(), rect.bottom(), bottomRight));
    gVertices.push_back(vertex(rect.x, rect.bottom(), bottomLeft));
    SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
}

void muiDrawLine(float x1, float y1, float x2, float y2, float thickness, MuiColor color)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) {
        return;
    }

    // Quad around the segment.
    float nx = -dy / length * thickness / 2.0f;
    float ny = dx / length * thickness / 2.0f;
    gVertices.clear();
    gVertices.push_back(vertex(x1 + nx, y1 + ny, color));
    gVertices.push_back(vertex(x2 + nx, y2 + ny, color));
    gVertices.push_back(vertex(x2 - nx, y2 - ny, color));
    gVertices.push_back(vertex(x1 + nx, y1 + ny, color));
    gVertices.push_back(vertex(x2 - nx, y2 - ny, color));
    gVertices.push_back(vertex(x1 - nx, y1 - ny, color));
    SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
}

void muiFillConvex(const std::vector<SDL_FPoint>& points, MuiColor color)
{
    if (points.size() < 3) {
        return;
    }

    float centerX = 0.0f;
    float centerY = 0.0f;
    for (const SDL_FPoint& point : points) {
        centerX += point.x;
        centerY += point.y;
    }
    fillPolygon(points, centerX / points.size(), centerY / points.size(), color);
}

void muiDrawPolyline(const std::vector<SDL_FPoint>& points, float thickness, MuiColor color, bool closed)
{
    size_t count = points.size();
    size_t segments = closed ? count : count - 1;
    for (size_t index = 0; count > 1 && index < segments; index++) {
        const SDL_FPoint& a = points[index];
        const SDL_FPoint& b = points[(index + 1) % count];
        muiDrawLine(a.x, a.y, b.x, b.y, thickness, color);
    }

    for (const SDL_FPoint& point : points) {
        muiFillCircle(point.x, point.y, thickness / 2.0f, color);
    }
}

void muiPushClip(const MuiRect& rect)
{
    SDL_Rect clip = {
        static_cast<int>(std::floor(rect.x)),
        static_cast<int>(std::floor(rect.y)),
        static_cast<int>(std::ceil(rect.w)),
        static_cast<int>(std::ceil(rect.h)),
    };

    if (!gClipStack.empty()) {
        SDL_Rect intersection;
        if (!SDL_IntersectRect(&clip, &gClipStack.back(), &intersection)) {
            intersection = { clip.x, clip.y, 0, 0 };
        }
        clip = intersection;
    }

    gClipStack.push_back(clip);
    SDL_RenderSetClipRect(gRenderer, &clip);
}

void muiPopClip()
{
    if (!gClipStack.empty()) {
        gClipStack.pop_back();
    }

    SDL_RenderSetClipRect(gRenderer, gClipStack.empty() ? nullptr : &gClipStack.back());
}

std::u32string muiDecodeGameText(const char* text)
{
    std::u32string result;
    if (text == nullptr) {
        return result;
    }

    bool cp1251 = gameTextIsCp1251();
    for (const unsigned char* ch = reinterpret_cast<const unsigned char*>(text); *ch != '\0'; ch++) {
        if (*ch < 0x80) {
            result.push_back(*ch);
        } else if (cp1251) {
            result.push_back(kCp1251[*ch - 0x80]);
        } else if (*ch < 0xA0) {
            result.push_back(kCp1252Low[*ch - 0x80]);
        } else {
            result.push_back(*ch);
        }
    }
    return result;
}

float muiFitTextSize(const std::u32string& text, float width, float size, float minSize)
{
    while (size > minSize && muiTextWidth(text, size) > width) {
        size -= 0.5f;
    }
    return size;
}

std::string muiEncodeGameText(const std::u32string& text)
{
    bool cp1251 = gameTextIsCp1251();
    std::string result;
    for (char32_t codepoint : text) {
        if (codepoint < 0x80) {
            result.push_back(static_cast<char>(codepoint));
            continue;
        }

        int found = -1;
        for (int index = 0; index < 0x80; index++) {
            char32_t mapped;
            if (cp1251) {
                mapped = kCp1251[index];
            } else if (index < 0x20) {
                mapped = kCp1252Low[index];
            } else {
                mapped = static_cast<char32_t>(0x80 + index);
            }

            if (mapped == codepoint) {
                found = 0x80 + index;
                break;
            }
        }

        // Not in the game's charset.
        if (found != -1) {
            result.push_back(static_cast<char>(found));
        }
    }
    return result;
}

std::u32string muiDecodeUtf8(const char* text)
{
    std::u32string result;
    if (text == nullptr) {
        return result;
    }

    const unsigned char* ch = reinterpret_cast<const unsigned char*>(text);
    while (*ch != '\0') {
        char32_t codepoint;
        int extra;
        if (*ch < 0x80) {
            codepoint = *ch;
            extra = 0;
        } else if ((*ch & 0xE0) == 0xC0) {
            codepoint = *ch & 0x1F;
            extra = 1;
        } else if ((*ch & 0xF0) == 0xE0) {
            codepoint = *ch & 0x0F;
            extra = 2;
        } else {
            codepoint = *ch & 0x07;
            extra = 3;
        }
        ch++;
        for (int index = 0; index < extra && *ch != '\0'; index++, ch++) {
            codepoint = (codepoint << 6) | (*ch & 0x3F);
        }
        result.push_back(codepoint);
    }
    return result;
}

float muiLineHeight(float size)
{
    FontAtlas* atlas = atlasForSize(size);
    return atlas != nullptr ? atlas->lineHeight : size * 1.2f;
}

float muiTextWidth(const std::u32string& text, float size)
{
    FontAtlas* atlas = atlasForSize(size);
    if (atlas == nullptr) {
        return 0.0f;
    }

    float width = 0.0f;
    for (char32_t codepoint : text) {
        int rangeIndex;
        int charIndex;
        const stbtt_packedchar* glyph = findGlyph(atlas, codepoint, &rangeIndex, &charIndex);
        if (glyph == nullptr) {
            glyph = findGlyph(atlas, U'?', &rangeIndex, &charIndex);
        }
        if (glyph != nullptr) {
            width += glyph->xadvance;
        }
    }
    return width;
}

void muiDrawText(const std::u32string& text, float x, float y, float size, MuiColor color)
{
    FontAtlas* atlas = atlasForSize(size);
    if (atlas == nullptr || text.empty()) {
        return;
    }

    gVertices.clear();

    // Snap baseline to pixels for crisp text.
    float penX = std::round(x);
    float penY = std::round(y + atlas->ascent);
    for (char32_t codepoint : text) {
        int rangeIndex;
        int charIndex;
        const stbtt_packedchar* glyph = findGlyph(atlas, codepoint, &rangeIndex, &charIndex);
        if (glyph == nullptr) {
            glyph = findGlyph(atlas, U'?', &rangeIndex, &charIndex);
            if (glyph == nullptr) {
                continue;
            }
        }

        stbtt_aligned_quad quad;
        stbtt_GetPackedQuad(atlas->chars[rangeIndex].data(), atlas->width, atlas->height, charIndex, &penX, &penY, &quad, 0);

        gVertices.push_back(vertex(quad.x0, quad.y0, color, quad.s0, quad.t0));
        gVertices.push_back(vertex(quad.x1, quad.y0, color, quad.s1, quad.t0));
        gVertices.push_back(vertex(quad.x1, quad.y1, color, quad.s1, quad.t1));
        gVertices.push_back(vertex(quad.x0, quad.y0, color, quad.s0, quad.t0));
        gVertices.push_back(vertex(quad.x1, quad.y1, color, quad.s1, quad.t1));
        gVertices.push_back(vertex(quad.x0, quad.y1, color, quad.s0, quad.t1));
    }

    SDL_RenderGeometry(gRenderer, atlas->texture, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
}

void muiDrawTextAligned(const std::u32string& text, const MuiRect& rect, float size, MuiColor color, MuiAlign horizontal, MuiAlign vertical)
{
    float width = muiTextWidth(text, size);
    float height = muiLineHeight(size);

    float x = rect.x;
    if (horizontal == MuiAlign::Center) {
        x = rect.x + (rect.w - width) / 2.0f;
    } else if (horizontal == MuiAlign::End) {
        x = rect.right() - width;
    }

    float y = rect.y;
    if (vertical == MuiAlign::Center) {
        y = rect.y + (rect.h - height) / 2.0f;
    } else if (vertical == MuiAlign::End) {
        y = rect.bottom() - height;
    }

    muiDrawText(text, x, y, size, color);
}

// Palette changes (fades, other palettes), textures follow it.
static void checkPalette()
{
    if (gPaletteCheckFrame != gFrame) {
        gPaletteCheckFrame = gFrame;
        unsigned char* palette = directDrawGetPalette();
        if (memcmp(palette, gPalette, sizeof(gPalette)) != 0) {
            memcpy(gPalette, palette, sizeof(gPalette));
            gPaletteVersion++;
        }
    }
}

// Converts 8-bit image to texture [entry] (created or reused).
static bool uploadIndexed(ArtTexture& entry, const unsigned char* data, int width, int height)
{
    std::vector<Uint32> pixels(static_cast<size_t>(width) * height);
    for (size_t index = 0; index < pixels.size(); index++) {
        unsigned char color = data[index];
        if (color == 0) {
            pixels[index] = 0;
        } else {
            // Palette is 6 bits per channel.
            Uint32 r = gPalette[color * 3] << 2;
            Uint32 g = gPalette[color * 3 + 1] << 2;
            Uint32 b = gPalette[color * 3 + 2] << 2;
            pixels[index] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    if (entry.texture != nullptr && (entry.width != width || entry.height != height)) {
        SDL_DestroyTexture(entry.texture);
        entry.texture = nullptr;
    }

    if (entry.texture == nullptr) {
        entry.texture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (entry.texture == nullptr) {
            return false;
        }
        SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeNearest);
    }

    SDL_UpdateTexture(entry.texture, nullptr, pixels.data(), width * 4);
    entry.width = width;
    entry.height = height;
    entry.paletteVersion = gPaletteVersion;
    return true;
}

SDL_Texture* muiIndexedTexture(const std::string& key, const unsigned char* data, int width, int height, unsigned int version)
{
    if (gRenderer == nullptr || data == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    checkPalette();

    IndexedTexture& entry = gIndexedTextures[key];
    if (entry.base.texture != nullptr && entry.version == version && entry.base.paletteVersion == gPaletteVersion
        && entry.base.width == width && entry.base.height == height) {
        return entry.base.texture;
    }

    if (!uploadIndexed(entry.base, data, width, height)) {
        return nullptr;
    }
    entry.version = version;
    return entry.base.texture;
}

SDL_Texture* muiRgbaTexture(const std::string& key, const unsigned char* data, int width, int height, unsigned int version)
{
    if (gRenderer == nullptr || data == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    IndexedTexture& entry = gIndexedTextures["rgba." + key];
    if (entry.base.texture != nullptr && entry.version == version && entry.base.width == width && entry.base.height == height) {
        return entry.base.texture;
    }

    if (entry.base.texture != nullptr && (entry.base.width != width || entry.base.height != height)) {
        SDL_DestroyTexture(entry.base.texture);
        entry.base.texture = nullptr;
    }

    if (entry.base.texture == nullptr) {
        entry.base.texture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (entry.base.texture == nullptr) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(entry.base.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(entry.base.texture, SDL_ScaleModeNearest);
    }

    SDL_UpdateTexture(entry.base.texture, nullptr, data, width * 4);
    entry.base.width = width;
    entry.base.height = height;
    entry.version = version;
    return entry.base.texture;
}

SDL_Texture* muiArtTexture(const FrmId& frmId, int frame, int* width, int* height, int rotation)
{
    if (gRenderer == nullptr) {
        return nullptr;
    }

    checkPalette();

    auto key = std::make_pair(frmId.fid(), frame * 8 + rotation);
    ArtTexture& entry = gArtTextures[key];
    if (entry.texture != nullptr && entry.paletteVersion == gPaletteVersion) {
        *width = entry.width;
        *height = entry.height;
        return entry.texture;
    }

    CacheEntry* handle;
    Art* art = artLock(frmId, &handle);
    if (art == nullptr) {
        return nullptr;
    }

    Rotation artRotation = static_cast<Rotation>(rotation);
    int artWidth = artGetWidth(art, frame, artRotation);
    int artHeight = artGetHeight(art, frame, artRotation);
    unsigned char* data = artGetFrameData(art, frame, artRotation);
    if (data == nullptr || artWidth <= 0 || artHeight <= 0) {
        artUnlock(handle);
        return nullptr;
    }

    bool uploaded = uploadIndexed(entry, data, artWidth, artHeight);
    artUnlock(handle);
    if (!uploaded) {
        return nullptr;
    }

    *width = artWidth;
    *height = artHeight;
    return entry.texture;
}

SDL_Texture* muiPictureTexture(const FrmId& frmId, int* width, int* height)
{
    *width = 0;
    *height = 0;
    if (gRenderer == nullptr) {
        return nullptr;
    }
    const char* path = frmId.hasFid() ? nullptr : frmId.filePath();
    if (!frmId.hasFid() && path == nullptr) {
        return nullptr;
    }

    checkPalette();

    auto key = std::make_tuple(frmId.fid(), static_cast<int>(frmId.objectType()),
        path != nullptr ? std::string(path) : std::string());
    ArtTexture& entry = gPictureTextures[key];
    if (entry.missing) {
        return nullptr;
    }
    if (entry.texture != nullptr && entry.paletteVersion == gPaletteVersion) {
        *width = entry.width;
        *height = entry.height;
        return entry.texture;
    }

    // Path based art too (high resolution pictures of f2_res.dat).
    FrmImage image;
    if (!image.lock(frmId)) {
        // Asked every frame (a fallback follows): the files aren't searched
        // again.
        entry.missing = true;
        return nullptr;
    }

    int artWidth = image.getWidth();
    int artHeight = image.getHeight();
    unsigned char* data = image.getData();
    bool uploaded = data != nullptr && artWidth > 0 && artHeight > 0 && uploadIndexed(entry, data, artWidth, artHeight);
    if (!uploaded) {
        return nullptr;
    }

    SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeLinear);
    *width = artWidth;
    *height = artHeight;
    return entry.texture;
}

SDL_Texture* muiLineArtTexture(const FrmId& frmId, int* width, int* height)
{
    if (gRenderer == nullptr) {
        return nullptr;
    }

    ArtTexture& entry = gLineArtTextures[frmId.fid()];
    if (entry.texture != nullptr) {
        *width = entry.width;
        *height = entry.height;
        return entry.texture;
    }

    CacheEntry* handle;
    Art* art = artLock(frmId, &handle);
    if (art == nullptr) {
        return nullptr;
    }

    int artWidth = artGetWidth(art, 0, ROTATION_NE);
    int artHeight = artGetHeight(art, 0, ROTATION_NE);
    unsigned char* data = artGetFrameData(art, 0, ROTATION_NE);
    if (data == nullptr || artWidth <= 0 || artHeight <= 0) {
        artUnlock(handle);
        return nullptr;
    }

    // Ink is what is much darker than the paper (the game's color table, so
    // fades don't matter): opaque below the paper's darkest tones, fading
    // out towards them.
    constexpr float kInk = 27.0f;
    constexpr float kPaper = 72.0f;
    Uint32 colors[256];
    colors[0] = 0;
    for (int index = 1; index < 256; index++) {
        float r = _cmap[index * 3] * 4.0f;
        float g = _cmap[index * 3 + 1] * 4.0f;
        float b = _cmap[index * 3 + 2] * 4.0f;
        float luminance = 0.299f * r + 0.587f * g + 0.114f * b;
        float alpha = std::clamp((kPaper - luminance) / (kPaper - kInk), 0.0f, 1.0f);
        colors[index] = (static_cast<Uint32>(alpha * 255.0f + 0.5f) << 24) | 0x00FFFFFF;
    }

    std::vector<Uint32> pixels(static_cast<size_t>(artWidth) * artHeight);
    for (size_t index = 0; index < pixels.size(); index++) {
        pixels[index] = colors[data[index]];
    }
    artUnlock(handle);

    entry.texture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, artWidth, artHeight);
    if (entry.texture == nullptr) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeLinear);
    SDL_UpdateTexture(entry.texture, nullptr, pixels.data(), artWidth * 4);

    entry.width = artWidth;
    entry.height = artHeight;
    *width = artWidth;
    *height = artHeight;
    return entry.texture;
}

void muiDrawTexture(SDL_Texture* texture, const MuiRect& rect, MuiColor tint)
{
    if (texture == nullptr) {
        return;
    }

    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture, tint.a);
    SDL_FRect dest = { rect.x, rect.y, rect.w, rect.h };
    SDL_RenderCopyF(gRenderer, texture, nullptr, &dest);
}

void muiDrawTexturePart(SDL_Texture* texture, const SDL_Rect& source, const MuiRect& rect, MuiColor tint)
{
    if (texture == nullptr || source.w <= 0 || source.h <= 0) {
        return;
    }

    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture, tint.a);
    SDL_FRect dest = { rect.x, rect.y, rect.w, rect.h };
    SDL_RenderCopyF(gRenderer, texture, &source, &dest);
}

void muiDrawTextureCover(SDL_Texture* texture, const MuiRect& rect, MuiColor tint)
{
    if (texture == nullptr || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    int width;
    int height;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) != 0 || width <= 0 || height <= 0) {
        return;
    }

    float sourceAspect = static_cast<float>(width) / height;
    float targetAspect = rect.w / rect.h;
    SDL_Rect source = { 0, 0, width, height };
    if (sourceAspect > targetAspect) {
        source.w = std::max(1, static_cast<int>(std::round(height * targetAspect)));
        source.x = (width - source.w) / 2;
    } else if (sourceAspect < targetAspect) {
        source.h = std::max(1, static_cast<int>(std::round(width / targetAspect)));
        source.y = (height - source.h) / 2;
    }

    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture, tint.a);
    SDL_FRect dest = { rect.x, rect.y, rect.w, rect.h };
    SDL_RenderCopyF(gRenderer, texture, &source, &dest);
}

MuiRect muiFitRect(const MuiRect& rect, float width, float height)
{
    if (width <= 0.0f || height <= 0.0f) {
        return rect;
    }

    float scale = std::min(rect.w / width, rect.h / height);
    float w = width * scale;
    float h = height * scale;
    return { rect.x + (rect.w - w) / 2.0f, rect.y + (rect.h - h) / 2.0f, w, h };
}

std::vector<std::u32string> muiWrapText(const std::u32string& text, float width, float size)
{
    std::vector<std::u32string> lines;

    // Explicit line breaks first.
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(U'\n', start);
        std::u32string paragraph = text.substr(start, end == std::u32string::npos ? std::u32string::npos : end - start);

        std::u32string line;
        size_t position = 0;
        while (position < paragraph.size()) {
            size_t space = paragraph.find(U' ', position);
            std::u32string word = paragraph.substr(position, space == std::u32string::npos ? std::u32string::npos : space - position);
            position = space == std::u32string::npos ? paragraph.size() : space + 1;

            std::u32string candidate = line.empty() ? word : line + U" " + word;
            if (!line.empty() && muiTextWidth(candidate, size) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        lines.push_back(line);

        if (end == std::u32string::npos) {
            break;
        }
        start = end + 1;
    }

    return lines;
}

} // namespace fallout
