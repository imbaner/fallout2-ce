#include "mui_icons.h"

#include <cmath>
#include <map>
#include <tuple>
#include <vector>

namespace fallout {

namespace {

    constexpr float kPi = 3.14159265f;

    // Draws in icon units: the icon square is -0.5..0.5 on both axes, y goes
    // down, angles go clockwise from +x (degrees).
    class Pen {
    public:
        Pen(float x, float y, float size, float thickness, MuiColor color)
            : cx(x)
            , cy(y)
            , size(size)
            , thickness(thickness)
            , color(color)
        {
        }

        SDL_FPoint p(float x, float y) const { return { cx + x * size, cy + y * size }; }

        void line(float x1, float y1, float x2, float y2) const
        {
            poly({ { x1, y1 }, { x2, y2 } });
        }

        void poly(std::initializer_list<SDL_FPoint> units, bool closed = false) const
        {
            std::vector<SDL_FPoint> points;
            for (const SDL_FPoint& unit : units) {
                points.push_back(p(unit.x, unit.y));
            }
            muiDrawPolyline(points, thickness, color, closed);
        }

        void fill(std::initializer_list<SDL_FPoint> units) const
        {
            std::vector<SDL_FPoint> points;
            for (const SDL_FPoint& unit : units) {
                points.push_back(p(unit.x, unit.y));
            }
            muiFillConvex(points, color);
        }

        std::vector<SDL_FPoint> arcPoints(float x, float y, float radius, float from, float to) const
        {
            int segments = std::max(static_cast<int>(std::fabs(to - from) / 15.0f), 2);
            std::vector<SDL_FPoint> points;
            for (int step = 0; step <= segments; step++) {
                float angle = (from + (to - from) * step / segments) * kPi / 180.0f;
                points.push_back(p(x + std::cos(angle) * radius, y + std::sin(angle) * radius));
            }
            return points;
        }

        void arc(float x, float y, float radius, float from, float to) const
        {
            muiDrawPolyline(arcPoints(x, y, radius, from, to), thickness, color);
        }

        // Arc with an arrow head at its end, pointing along the arc.
        void arcArrow(float x, float y, float radius, float from, float to) const
        {
            arc(x, y, radius, from, to);

            float angle = to * kPi / 180.0f;
            float direction = to > from ? 1.0f : -1.0f;
            float endX = x + std::cos(angle) * radius;
            float endY = y + std::sin(angle) * radius;
            // Tangent in the direction of travel.
            float tangentX = -std::sin(angle) * direction;
            float tangentY = std::cos(angle) * direction;
            float head = 0.2f;
            float spread = 0.6f;
            float leftX = -tangentX * head + -tangentY * head * spread;
            float leftY = -tangentY * head + tangentX * head * spread;
            float rightX = -tangentX * head + tangentY * head * spread;
            float rightY = -tangentY * head + -tangentX * head * spread;
            poly({ { endX + leftX, endY + leftY }, { endX, endY }, { endX + rightX, endY + rightY } });
        }

        void circle(float x, float y, float radius) const
        {
            muiStrokeCircle(cx + x * size, cy + y * size, radius * size + thickness / 2.0f, thickness, color);
        }

        void dot(float x, float y, float radius) const
        {
            muiFillCircle(cx + x * size, cy + y * size, radius * size, color);
        }

        void rect(float x, float y, float w, float h, float radius) const
        {
            MuiRect rect = { cx + x * size - thickness / 2.0f, cy + y * size - thickness / 2.0f, w * size + thickness, h * size + thickness };
            muiStrokeRoundRect(rect, radius * size + thickness / 2.0f, thickness, color);
        }

        // Quadratic curve.
        void curve(SDL_FPoint from, SDL_FPoint control, SDL_FPoint to) const
        {
            std::vector<SDL_FPoint> points;
            constexpr int kSegments = 10;
            for (int step = 0; step <= kSegments; step++) {
                float t = static_cast<float>(step) / kSegments;
                float a = (1 - t) * (1 - t);
                float b = 2 * (1 - t) * t;
                float c = t * t;
                points.push_back(p(a * from.x + b * control.x + c * to.x, a * from.y + b * control.y + c * to.y));
            }
            muiDrawPolyline(points, thickness, color);
        }

        float cx;
        float cy;
        float size;
        float thickness;
        MuiColor color;
    };

    // MARK: HUD

    void drawMenu(const Pen& pen)
    {
        for (int line = -1; line <= 1; line++) {
            pen.line(-0.42f, line * 0.3f, 0.42f, line * 0.3f);
        }
    }

    // Floppy disk with an arrow into (save) or out of (load) it.
    void drawFloppy(const Pen& pen, bool save)
    {
        pen.poly({ { -0.4f, -0.32f }, { 0.24f, -0.32f }, { 0.4f, -0.16f }, { 0.4f, 0.48f }, { -0.4f, 0.48f } }, true);
        pen.poly({ { -0.22f, 0.48f }, { -0.22f, 0.2f }, { 0.22f, 0.2f }, { 0.22f, 0.48f } });

        pen.line(0.0f, -0.52f, 0.0f, 0.02f);
        if (save) {
            pen.poly({ { -0.16f, -0.14f }, { 0.0f, 0.02f }, { 0.16f, -0.14f } });
        } else {
            pen.poly({ { -0.16f, -0.36f }, { 0.0f, -0.52f }, { 0.16f, -0.36f } });
        }
    }

    // Backpack.
    void drawInventory(const Pen& pen)
    {
        pen.arc(0.0f, -0.28f, 0.17f, 180.0f, 360.0f);
        pen.rect(-0.42f, -0.28f, 0.84f, 0.76f, 0.16f);
        pen.line(-0.42f, 0.0f, 0.42f, 0.0f);
        pen.poly({ { -0.18f, 0.0f }, { -0.18f, 0.2f }, { 0.18f, 0.2f }, { 0.18f, 0.0f } });
    }

    // Head and shoulders.
    void drawCharacter(const Pen& pen)
    {
        pen.circle(0.0f, -0.22f, 0.17f);
        pen.arc(0.0f, 0.46f, 0.4f, 180.0f, 360.0f);
        pen.line(-0.4f, 0.46f, 0.4f, 0.46f);
    }

    // Wrist computer: screen, knob and straps.
    void drawPipboy(const Pen& pen)
    {
        pen.rect(-0.46f, -0.3f, 0.92f, 0.6f, 0.08f);
        pen.rect(-0.34f, -0.18f, 0.46f, 0.36f, 0.04f);
        pen.circle(0.3f, -0.06f, 0.06f);
        pen.line(0.26f, 0.16f, 0.34f, 0.16f);
        for (float x : { -0.26f, 0.04f }) {
            pen.line(x, -0.3f, x, -0.46f);
            pen.line(x, 0.3f, x, 0.46f);
        }
    }

    // Folded paper map.
    void drawMap(const Pen& pen)
    {
        pen.poly({ { -0.46f, -0.32f }, { -0.16f, -0.42f }, { 0.16f, -0.32f }, { 0.46f, -0.42f }, { 0.46f, 0.32f }, { 0.16f, 0.42f }, { -0.16f, 0.32f }, { -0.46f, 0.42f } }, true);
        pen.line(-0.16f, -0.42f, -0.16f, 0.32f);
        pen.line(0.16f, -0.32f, 0.16f, 0.42f);
    }

    // Open book (Skilldex).
    void drawSkills(const Pen& pen)
    {
        pen.poly({ { 0.0f, -0.28f }, { -0.46f, -0.38f }, { -0.46f, 0.34f }, { 0.0f, 0.44f }, { 0.46f, 0.34f }, { 0.46f, -0.38f } }, true);
        pen.line(0.0f, -0.28f, 0.0f, 0.44f);
        pen.line(-0.34f, -0.14f, -0.12f, -0.1f);
        pen.line(-0.34f, 0.04f, -0.12f, 0.08f);
        pen.line(0.34f, -0.14f, 0.12f, -0.1f);
        pen.line(0.34f, 0.04f, 0.12f, 0.08f);
    }

    // Light bulb.
    void drawHighlight(const Pen& pen)
    {
        pen.arc(0.0f, -0.14f, 0.3f, 135.0f, 405.0f);
        pen.line(-0.21f, 0.07f, -0.13f, 0.24f);
        pen.line(0.21f, 0.07f, 0.13f, 0.24f);
        pen.line(-0.13f, 0.24f, 0.13f, 0.24f);
        pen.line(-0.11f, 0.36f, 0.11f, 0.36f);
        pen.line(-0.06f, 0.47f, 0.06f, 0.47f);
    }

    void drawEye(const Pen& pen)
    {
        pen.curve({ -0.48f, 0.0f }, { 0.0f, -0.46f }, { 0.48f, 0.0f });
        pen.curve({ -0.48f, 0.0f }, { 0.0f, 0.46f }, { 0.48f, 0.0f });
        pen.circle(0.0f, 0.0f, 0.1f);
    }

    // Eye crossed out: not seen.
    void drawSneak(const Pen& pen)
    {
        drawEye(pen);
        pen.line(-0.38f, 0.38f, 0.38f, -0.38f);
    }

    // Terminal prompt.
    void drawLog(const Pen& pen)
    {
        pen.rect(-0.46f, -0.36f, 0.92f, 0.72f, 0.08f);
        pen.poly({ { -0.28f, -0.14f }, { -0.1f, 0.02f }, { -0.28f, 0.18f } });
        pen.line(0.0f, 0.18f, 0.26f, 0.18f);
    }

    // Play to the end.
    void drawEndTurn(const Pen& pen)
    {
        pen.poly({ { -0.36f, -0.34f }, { 0.16f, 0.0f }, { -0.36f, 0.34f } }, true);
        pen.line(0.32f, -0.34f, 0.32f, 0.34f);
    }

    // Truce flag.
    void drawEndCombat(const Pen& pen)
    {
        pen.line(-0.34f, -0.46f, -0.34f, 0.48f);
        pen.poly({ { -0.34f, -0.42f }, { 0.42f, -0.42f }, { 0.24f, -0.18f }, { 0.42f, 0.06f }, { -0.34f, 0.06f } });
    }

    // Two arrows chasing each other.
    void drawSwapHands(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.36f, 200.0f, 330.0f);
        pen.arcArrow(0.0f, 0.0f, 0.36f, 20.0f, 150.0f);
    }

    // Lightning bolt (filled).
    void drawActionPoints(const Pen& pen)
    {
        pen.fill({ { 0.16f, -0.5f }, { -0.3f, 0.08f }, { 0.08f, 0.08f } });
        pen.fill({ { -0.08f, -0.08f }, { 0.3f, -0.08f }, { -0.16f, 0.5f } });
    }

    // Heart (filled).
    void drawHitPoints(const Pen& pen)
    {
        pen.dot(-0.2f, -0.14f, 0.22f);
        pen.dot(0.2f, -0.14f, 0.22f);
        pen.fill({ { -0.41f, -0.06f }, { 0.41f, -0.06f }, { 0.0f, 0.44f } });
    }

    // Crosshair.
    void drawAim(const Pen& pen)
    {
        pen.circle(0.0f, 0.0f, 0.28f);
        pen.line(0.0f, -0.48f, 0.0f, -0.16f);
        pen.line(0.0f, 0.16f, 0.0f, 0.48f);
        pen.line(-0.48f, 0.0f, -0.16f, 0.0f);
        pen.line(0.16f, 0.0f, 0.48f, 0.0f);
        pen.dot(0.0f, 0.0f, 0.05f);
    }

    // Bullet.
    void drawAmmo(const Pen& pen, float x, float y, float scale)
    {
        float half = 0.17f * scale;
        float top = y - 0.15f * scale;
        float bottom = y + 0.5f * scale;
        float tip = y - 0.5f * scale;
        pen.poly({ { x - half, bottom }, { x - half, top }, { x, tip }, { x + half, top }, { x + half, bottom } }, true);
    }

    // Circular arrow around a bullet.
    void drawReload(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.4f, -60.0f, 230.0f);
        drawAmmo(pen, 0.0f, 0.02f, 0.44f);
    }

    // Fist from the front: four knuckles, thumb across the fingers.
    void drawFist(const Pen& pen)
    {
        for (int finger = 0; finger < 4; finger++) {
            float x = -0.27f + finger * 0.18f;
            pen.arc(x, -0.14f, 0.09f, 180.0f, 360.0f);
            pen.line(x + 0.09f, -0.14f, x + 0.09f, finger == 3 ? 0.2f : 0.02f);
        }
        pen.poly({ { -0.36f, -0.14f }, { -0.36f, 0.24f }, { -0.2f, 0.44f }, { 0.24f, 0.44f }, { 0.36f, 0.24f }, { 0.36f, 0.2f } });
        pen.poly({ { -0.36f, 0.08f }, { 0.08f, 0.08f }, { 0.16f, 0.2f } });
    }

    // Kettlebell.
    void drawWeight(const Pen& pen)
    {
        pen.arc(0.0f, -0.24f, 0.2f, 160.0f, 380.0f);
        pen.circle(0.0f, 0.16f, 0.3f);
        pen.line(-0.22f, 0.46f, 0.22f, 0.46f);
    }

    // Front part of a wide flat ellipse with heads at both ends, pointing
    // sideways.
    void drawTurn(const Pen& pen)
    {
        constexpr float kRadiusX = 0.8f;
        constexpr float kRadiusY = 0.3f;
        constexpr float kCenterY = -0.2f;
        constexpr float kFrom = 55.0f;
        constexpr float kTo = 125.0f;

        std::vector<SDL_FPoint> points;
        for (int step = 0; step <= 12; step++) {
            float angle = (kFrom + (kTo - kFrom) * step / 12.0f) * kPi / 180.0f;
            points.push_back(pen.p(std::cos(angle) * kRadiusX, kCenterY + std::sin(angle) * kRadiusY));
        }
        muiDrawPolyline(points, pen.thickness, pen.color);

        // Heads point along the ellipse, away from its middle.
        for (float end : { kFrom, kTo }) {
            float angle = end * kPi / 180.0f;
            float x = std::cos(angle) * kRadiusX;
            float y = kCenterY + std::sin(angle) * kRadiusY;
            float direction = end == kFrom ? -1.0f : 1.0f;
            float tangentX = -std::sin(angle) * kRadiusX * direction;
            float tangentY = std::cos(angle) * kRadiusY * direction;
            float length = std::sqrt(tangentX * tangentX + tangentY * tangentY);
            tangentX /= length;
            tangentY /= length;
            float head = 0.14f;
            pen.poly({ { x - tangentX * head - tangentY * head * 0.8f, y - tangentY * head + tangentX * head * 0.8f },
                { x, y },
                { x - tangentX * head + tangentY * head * 0.8f, y - tangentY * head - tangentX * head * 0.8f } });
        }
    }

    void drawBack(const Pen& pen)
    {
        pen.line(-0.42f, 0.0f, 0.42f, 0.0f);
        pen.poly({ { -0.06f, -0.36f }, { -0.42f, 0.0f }, { -0.06f, 0.36f } });
    }

    // MARK: Dialog

    // Speech bubble: one outline, the tail opens out of the bottom edge.
    void drawTalk(const Pen& pen)
    {
        constexpr float kLeft = -0.5f;
        constexpr float kTop = -0.5f;
        constexpr float kRight = 0.5f;
        constexpr float kBottom = 0.22f;
        constexpr float kRadius = 0.18f;

        std::vector<SDL_FPoint> points;
        auto arc = [&](float x, float y, float from, float to) {
            std::vector<SDL_FPoint> arcPoints = pen.arcPoints(x, y, kRadius, from, to);
            points.insert(points.end(), arcPoints.begin(), arcPoints.end());
        };

        // From the tail's right side clockwise round the corners.
        points.push_back(pen.p(-0.02f, kBottom));
        arc(kRight - kRadius, kBottom - kRadius, 90.0f, 0.0f);
        arc(kRight - kRadius, kTop + kRadius, 0.0f, -90.0f);
        arc(kLeft + kRadius, kTop + kRadius, -90.0f, -180.0f);
        arc(kLeft + kRadius, kBottom - kRadius, 180.0f, 90.0f);
        points.push_back(pen.p(-0.25f, kBottom));
        points.push_back(pen.p(-0.38f, 0.5f));
        muiDrawPolyline(points, pen.thickness, pen.color, true);
    }

    // Two opposite arrows.
    void drawBarter(const Pen& pen)
    {
        float head = 0.28f;
        pen.line(-0.5f, -0.22f, 0.5f, -0.22f);
        pen.poly({ { 0.5f - head, -0.22f - head }, { 0.5f, -0.22f }, { 0.5f - head, -0.22f + head } });
        pen.line(0.5f, 0.22f, -0.5f, 0.22f);
        pen.poly({ { -0.5f + head, 0.22f - head }, { -0.5f, 0.22f }, { -0.5f + head, 0.22f + head } });
    }

    // Clock.
    void drawReview(const Pen& pen)
    {
        pen.circle(0.0f, 0.0f, 0.46f);
        pen.poly({ { 0.0f, -0.28f }, { 0.0f, 0.0f }, { 0.2f, 0.08f } });
    }

    // MARK: Filters

    // Pistol.
    void drawWeapon(const Pen& pen, float x, float y, float scale)
    {
        float top = y - 0.22f * scale;
        pen.line(x - 0.5f * scale, top, x + 0.5f * scale, top);
        pen.line(x - 0.28f * scale, top, x - 0.4f * scale, y + 0.5f * scale);
        pen.line(x - 0.05f * scale, top, x - 0.05f * scale, y + 0.12f * scale);
    }

    // Shield.
    void drawArmor(const Pen& pen)
    {
        pen.poly({ { -0.4f, -0.5f }, { 0.4f, -0.5f }, { 0.4f, 0.08f }, { 0.0f, 0.5f }, { -0.4f, 0.08f } }, true);
    }

    // Medical cross.
    void drawDrugs(const Pen& pen)
    {
        Pen thick = pen;
        thick.thickness *= 2.0f;
        thick.line(-0.5f, 0.0f, 0.5f, 0.0f);
        thick.line(0.0f, -0.5f, 0.0f, 0.5f);
    }

    // Box.
    void drawMisc(const Pen& pen)
    {
        pen.rect(-0.5f, -0.28f, 1.0f, 0.78f, 0.04f);
        pen.line(-0.5f, -0.03f, 0.5f, -0.03f);
    }

    // MARK: Action menu

    void drawCancel(const Pen& pen)
    {
        pen.line(-0.34f, -0.34f, 0.34f, 0.34f);
        pen.line(-0.34f, 0.34f, 0.34f, -0.34f);
    }

    void drawPlus(const Pen& pen)
    {
        pen.line(-0.36f, 0.0f, 0.36f, 0.0f);
        pen.line(0.0f, -0.36f, 0.0f, 0.36f);
    }

    void drawMinus(const Pen& pen)
    {
        pen.line(-0.36f, 0.0f, 0.36f, 0.0f);
    }

    void drawPlay(const Pen& pen)
    {
        pen.fill({ { -0.26f, -0.4f }, { 0.4f, 0.0f }, { -0.26f, 0.4f } });
    }

    void drawPause(const Pen& pen)
    {
        pen.fill({ { -0.34f, -0.38f }, { -0.1f, -0.38f }, { -0.1f, 0.38f }, { -0.34f, 0.38f } });
        pen.fill({ { 0.1f, -0.38f }, { 0.34f, -0.38f }, { 0.34f, 0.38f }, { 0.1f, 0.38f } });
    }

    void drawStop(const Pen& pen)
    {
        pen.fill({ { -0.32f, -0.32f }, { 0.32f, -0.32f }, { 0.32f, 0.32f }, { -0.32f, 0.32f } });
    }

    // Corners out (full screen) or in (back from it).
    void drawCorners(const Pen& pen, bool out)
    {
        float a = out ? 0.42f : 0.12f;
        float b = out ? 0.12f : 0.42f;
        for (float sx : { -1.0f, 1.0f }) {
            for (float sy : { -1.0f, 1.0f }) {
                pen.poly({ { sx * a, sy * b }, { sx * a, sy * a }, { sx * b, sy * a } });
            }
        }
    }

    // Pencil.
    void drawEdit(const Pen& pen)
    {
        pen.poly({ { -0.36f, 0.4f }, { -0.36f, 0.2f }, { 0.22f, -0.38f }, { 0.42f, -0.18f }, { -0.16f, 0.4f } }, true);
        pen.line(0.08f, -0.24f, 0.28f, -0.04f);
    }

    // Arrow down to the ground.
    void drawDrop(const Pen& pen)
    {
        pen.line(0.0f, -0.46f, 0.0f, 0.16f);
        pen.poly({ { -0.22f, -0.06f }, { 0.0f, 0.16f }, { 0.22f, -0.06f } });
        pen.line(-0.4f, 0.42f, 0.4f, 0.42f);
    }

    // Circular arrow around the object.
    void drawRotate(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.4f, 120.0f, 400.0f);
        pen.dot(0.0f, 0.0f, 0.09f);
    }

    // Open hand.
    void drawUse(const Pen& pen)
    {
        pen.rect(-0.26f, -0.02f, 0.5f, 0.48f, 0.16f);
        pen.line(-0.18f, -0.02f, -0.18f, -0.3f);
        pen.line(-0.04f, -0.02f, -0.04f, -0.44f);
        pen.line(0.1f, -0.02f, 0.1f, -0.42f);
        pen.line(0.24f, 0.04f, 0.24f, -0.26f);
        pen.line(-0.26f, 0.2f, -0.44f, -0.02f);
    }

    // Magazine with a bullet going out.
    void drawUnload(const Pen& pen)
    {
        pen.rect(-0.22f, 0.02f, 0.44f, 0.46f, 0.06f);
        pen.line(-0.22f, 0.18f, 0.22f, 0.18f);
        pen.line(0.0f, -0.08f, 0.0f, -0.48f);
        pen.poly({ { -0.18f, -0.3f }, { 0.0f, -0.48f }, { 0.18f, -0.3f } });
    }

    // Arrow into a wall.
    void drawPush(const Pen& pen)
    {
        pen.line(-0.46f, 0.0f, 0.12f, 0.0f);
        pen.poly({ { -0.08f, -0.2f }, { 0.12f, 0.0f }, { -0.08f, 0.2f } });
        pen.line(0.34f, -0.4f, 0.34f, 0.4f);
    }

    // MARK: Party orders

    // Two people, the front one bigger; the one behind shows only what the
    // front one doesn't cover.
    void drawPartyOrders(const Pen& pen)
    {
        pen.circle(0.2f, -0.26f, 0.12f);
        pen.arc(0.2f, 0.3f, 0.28f, 250.0f, 340.0f);
        pen.circle(-0.14f, -0.12f, 0.15f);
        pen.arc(-0.14f, 0.48f, 0.34f, 180.0f, 360.0f);
        pen.line(-0.48f, 0.48f, 0.2f, 0.48f);
    }

    // Pistol going down.
    void drawHolster(const Pen& pen)
    {
        drawWeapon(pen, -0.1f, -0.14f, 0.7f);
        pen.line(0.36f, -0.46f, 0.36f, 0.42f);
        pen.poly({ { 0.2f, 0.26f }, { 0.36f, 0.42f }, { 0.52f, 0.26f } });
    }

    // Four arrows to the center (or from it).
    void drawCrossArrows(const Pen& pen, bool inward)
    {
        for (int direction = 0; direction < 4; direction++) {
            float dx = direction == 0 ? 1.0f : (direction == 1 ? -1.0f : 0.0f);
            float dy = direction == 2 ? 1.0f : (direction == 3 ? -1.0f : 0.0f);
            float outer = 0.48f;
            float inner = 0.14f;
            pen.line(dx * outer, dy * outer, dx * inner, dy * inner);
            float tip = inward ? inner : outer;
            float back = inward ? inner + 0.14f : outer - 0.14f;
            pen.poly({ { dx * back - dy * 0.12f, dy * back - dx * 0.12f }, { dx * tip, dy * tip }, { dx * back + dy * 0.12f, dy * back + dx * 0.12f } });
        }
    }

    // Three bullets.
    void drawBurst(const Pen& pen)
    {
        drawAmmo(pen, -0.3f, 0.02f, 0.62f);
        drawAmmo(pen, 0.0f, 0.02f, 0.62f);
        drawAmmo(pen, 0.3f, 0.02f, 0.62f);
    }

    // Hand in a circular arrow: picking up by itself.
    void drawAutoLoot(const Pen& pen)
    {
        pen.arcArrow(0.0f, 0.0f, 0.46f, -70.0f, 220.0f);
        Pen small = pen;
        small.size *= 0.55f;
        small.cy += pen.size * 0.04f;
        drawUse(small);
    }

    // MARK: Skills

    // Open padlock with a keyhole.
    void drawLockpick(const Pen& pen)
    {
        pen.rect(-0.32f, -0.02f, 0.64f, 0.46f, 0.06f);
        pen.arc(0.0f, -0.12f, 0.2f, 180.0f, 330.0f);
        pen.line(-0.2f, -0.12f, -0.2f, -0.02f);
        pen.dot(0.0f, 0.16f, 0.06f);
        pen.line(0.0f, 0.18f, 0.0f, 0.3f);
    }

    // Hand taking a coin.
    void drawSteal(const Pen& pen)
    {
        Pen hand = pen;
        hand.size *= 0.82f;
        hand.cx -= pen.size * 0.08f;
        hand.cy += pen.size * 0.08f;
        drawUse(hand);
        pen.circle(0.32f, -0.3f, 0.12f);
    }

    // Mine with spikes.
    void drawTraps(const Pen& pen)
    {
        pen.arc(0.0f, 0.24f, 0.34f, 180.0f, 360.0f);
        pen.line(-0.46f, 0.24f, 0.46f, 0.24f);
        pen.rect(-0.08f, -0.24f, 0.16f, 0.14f, 0.02f);
        pen.line(-0.3f, -0.34f, -0.18f, -0.2f);
        pen.line(0.3f, -0.34f, 0.18f, -0.2f);
        pen.line(0.0f, -0.46f, 0.0f, -0.34f);
    }

    // Medical bag.
    void drawDoctor(const Pen& pen)
    {
        pen.arc(0.0f, -0.2f, 0.16f, 180.0f, 360.0f);
        pen.rect(-0.42f, -0.2f, 0.84f, 0.62f, 0.08f);
        pen.line(-0.14f, 0.11f, 0.14f, 0.11f);
        pen.line(0.0f, -0.03f, 0.0f, 0.25f);
    }

    // Computer screen.
    void drawScience(const Pen& pen)
    {
        pen.rect(-0.44f, -0.38f, 0.88f, 0.58f, 0.06f);
        pen.poly({ { -0.28f, -0.04f }, { -0.14f, -0.04f }, { -0.06f, -0.2f }, { 0.06f, 0.08f }, { 0.14f, -0.04f }, { 0.28f, -0.04f } });
        pen.line(0.0f, 0.2f, 0.0f, 0.36f);
        pen.line(-0.2f, 0.42f, 0.2f, 0.42f);
    }

    // Wrench.
    void drawRepair(const Pen& pen)
    {
        pen.line(-0.38f, 0.38f, 0.12f, -0.12f);
        pen.arc(0.24f, -0.24f, 0.17f, 225.0f, 495.0f);
    }

    // Crosshair in a frame corner marks: choose a target.
    void drawTarget(const Pen& pen)
    {
        pen.circle(0.0f, 0.0f, 0.2f);
        pen.dot(0.0f, 0.0f, 0.05f);
        pen.poly({ { -0.46f, -0.24f }, { -0.46f, -0.46f }, { -0.24f, -0.46f } });
        pen.poly({ { 0.24f, -0.46f }, { 0.46f, -0.46f }, { 0.46f, -0.24f } });
        pen.poly({ { 0.46f, 0.24f }, { 0.46f, 0.46f }, { 0.24f, 0.46f } });
        pen.poly({ { -0.24f, 0.46f }, { -0.46f, 0.46f }, { -0.46f, 0.24f } });
    }

} // namespace

// Draws the icon's lines and shapes.
static void drawIconShapes(MuiIcon icon, float x, float y, float size, float thickness, MuiColor color)
{
    Pen pen(x, y, size, thickness, color);

    switch (icon) {
    case MuiIcon::Menu:
    case MuiIcon::FilterAll:
        drawMenu(pen);
        break;
    case MuiIcon::QuickSave:
        drawFloppy(pen, true);
        break;
    case MuiIcon::QuickLoad:
        drawFloppy(pen, false);
        break;
    case MuiIcon::Inventory:
        drawInventory(pen);
        break;
    case MuiIcon::Character:
        drawCharacter(pen);
        break;
    case MuiIcon::Pipboy:
        drawPipboy(pen);
        break;
    case MuiIcon::Map:
        drawMap(pen);
        break;
    case MuiIcon::Skills:
        drawSkills(pen);
        break;
    case MuiIcon::Highlight:
        drawHighlight(pen);
        break;
    case MuiIcon::Sneak:
        drawSneak(pen);
        break;
    case MuiIcon::Log:
        drawLog(pen);
        break;
    case MuiIcon::EndTurn:
        drawEndTurn(pen);
        break;
    case MuiIcon::EndCombat:
        drawEndCombat(pen);
        break;
    case MuiIcon::SwapHands:
        drawSwapHands(pen);
        break;
    case MuiIcon::ActionPoints:
        drawActionPoints(pen);
        break;
    case MuiIcon::HitPoints:
        drawHitPoints(pen);
        break;
    case MuiIcon::Aim:
        drawAim(pen);
        break;
    case MuiIcon::Reload:
        drawReload(pen);
        break;
    case MuiIcon::Fist:
        drawFist(pen);
        break;
    case MuiIcon::Weight:
        drawWeight(pen);
        break;
    case MuiIcon::Back:
        drawBack(pen);
        break;
    case MuiIcon::Turn:
        drawTurn(pen);
        break;
    case MuiIcon::PartyOrders:
        drawPartyOrders(pen);
        break;
    case MuiIcon::Holster:
        drawHolster(pen);
        break;
    case MuiIcon::Regroup:
        drawCrossArrows(pen, true);
        break;
    case MuiIcon::Spread:
        drawCrossArrows(pen, false);
        break;
    case MuiIcon::Burst:
        drawBurst(pen);
        break;
    case MuiIcon::AutoLoot:
        drawAutoLoot(pen);
        break;
    case MuiIcon::Lockpick:
        drawLockpick(pen);
        break;
    case MuiIcon::Steal:
        drawSteal(pen);
        break;
    case MuiIcon::Traps:
        drawTraps(pen);
        break;
    case MuiIcon::Doctor:
        drawDoctor(pen);
        break;
    case MuiIcon::Science:
        drawScience(pen);
        break;
    case MuiIcon::Repair:
        drawRepair(pen);
        break;
    case MuiIcon::Target:
        drawTarget(pen);
        break;
    case MuiIcon::Talk:
        drawTalk(pen);
        break;
    case MuiIcon::Barter:
        drawBarter(pen);
        break;
    case MuiIcon::Review:
        drawReview(pen);
        break;
    case MuiIcon::FilterWeapons:
        drawWeapon(pen, 0.0f, 0.0f, 1.0f);
        break;
    case MuiIcon::FilterWeaponsAndAmmo:
        drawWeapon(pen, -0.2f, 0.0f, 0.7f);
        drawAmmo(pen, 0.38f, 0.0f, 0.6f);
        break;
    case MuiIcon::FilterArmor:
        drawArmor(pen);
        break;
    case MuiIcon::FilterDrugs:
        drawDrugs(pen);
        break;
    case MuiIcon::FilterAmmo:
        drawAmmo(pen, 0.0f, 0.0f, 1.0f);
        break;
    case MuiIcon::FilterMisc:
        drawMisc(pen);
        break;
    case MuiIcon::Cancel:
        drawCancel(pen);
        break;
    case MuiIcon::Drop:
        drawDrop(pen);
        break;
    case MuiIcon::Look:
        drawEye(pen);
        break;
    case MuiIcon::Rotate:
        drawRotate(pen);
        break;
    case MuiIcon::Use:
        drawUse(pen);
        break;
    case MuiIcon::Unload:
        drawUnload(pen);
        break;
    case MuiIcon::Push:
        drawPush(pen);
        break;
    case MuiIcon::Plus:
        drawPlus(pen);
        break;
    case MuiIcon::Minus:
        drawMinus(pen);
        break;
    case MuiIcon::Edit:
        drawEdit(pen);
        break;
    case MuiIcon::Play:
        drawPlay(pen);
        break;
    case MuiIcon::Pause:
        drawPause(pen);
        break;
    case MuiIcon::Stop:
        drawStop(pen);
        break;
    case MuiIcon::Expand:
        drawCorners(pen, true);
        break;
    case MuiIcon::Collapse:
        drawCorners(pen, false);
        break;
    case MuiIcon::Count:
        break;
    }
}

namespace {

    // An icon drawn in white into a texture (alpha premultiplied), tinted
    // when drawn. Icons are made of many lines and round joins; drawing them
    // again every frame was most of the HUD's time.
    struct IconTexture {
        SDL_Texture* texture = nullptr;
        int extent = 0;
    };

    using IconKey = std::tuple<int, int, int>;
    std::map<IconKey, IconTexture> gIconTextures;

    // Sizes change during animations; the cache is dropped when it grows
    // past this.
    constexpr size_t kMaxIconTextures = 256;

    const IconTexture* iconTexture(SDL_Renderer* renderer, MuiIcon icon, float size, float thickness)
    {
        IconKey key(static_cast<int>(icon), static_cast<int>(lroundf(size * 4.0f)), static_cast<int>(lroundf(thickness * 4.0f)));
        auto it = gIconTextures.find(key);
        if (it != gIconTextures.end()) {
            return it->second.texture != nullptr ? &(it->second) : nullptr;
        }

        if (gIconTextures.size() >= kMaxIconTextures) {
            muiIconsReset();
        }

        IconTexture& entry = gIconTextures[key];

        // Room for strokes and round joins sticking out of the icon square.
        entry.extent = static_cast<int>(std::ceil(size + thickness * 2.0f)) + 4;
        entry.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, entry.extent, entry.extent);
        if (entry.texture == nullptr) {
            return nullptr;
        }

        SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, entry.texture) != 0) {
            SDL_DestroyTexture(entry.texture);
            entry.texture = nullptr;
            return nullptr;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        // White with alpha blended over transparent black is premultiplied.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        float center = entry.extent / 2.0f;
        drawIconShapes(icon, center, center, size, thickness, muiRgb(0xFFFFFF));
        SDL_SetRenderTarget(renderer, previousTarget);

        SDL_SetTextureBlendMode(entry.texture, muiPremultipliedBlendMode());
        SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeNearest);
        return &entry;
    }

} // namespace

void muiDrawIcon(MuiIcon icon, float x, float y, float size, float thickness, MuiColor color)
{
    SDL_Renderer* renderer = muiDrawGetRenderer();
    const IconTexture* entry = renderer != nullptr ? iconTexture(renderer, icon, size, thickness) : nullptr;
    if (entry == nullptr) {
        drawIconShapes(icon, x, y, size, thickness, color);
        return;
    }

    // Premultiplied: the tint's alpha scales the color too.
    SDL_SetTextureColorMod(entry->texture, color.r * color.a / 255, color.g * color.a / 255, color.b * color.a / 255);
    SDL_SetTextureAlphaMod(entry->texture, color.a);

    // Whole pixels keep the icon as sharp as drawn.
    SDL_Rect dest = {
        static_cast<int>(lroundf(x - entry->extent / 2.0f)),
        static_cast<int>(lroundf(y - entry->extent / 2.0f)),
        entry->extent,
        entry->extent,
    };
    SDL_RenderCopy(renderer, entry->texture, nullptr, &dest);
}

void muiIconsReset()
{
    for (auto& entry : gIconTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gIconTextures.clear();
}

} // namespace fallout
