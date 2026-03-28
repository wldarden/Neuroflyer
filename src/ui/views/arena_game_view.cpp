#include <neuroflyer/ui/views/arena_game_view.h>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace neuroflyer {

namespace {

// 8 team colors, cycled for teams > 8
struct TeamColor {
    uint8_t r, g, b;
};

constexpr TeamColor TEAM_COLORS[] = {
    {100, 180, 255},  // blue
    {255, 100, 100},  // red
    {100, 255, 130},  // green
    {255, 220, 80},   // yellow
    {220, 130, 255},  // purple
    {255, 160, 80},   // orange
    {80, 255, 255},   // cyan
    {255, 130, 180},  // pink
};
constexpr int NUM_TEAM_COLORS = 8;

const TeamColor& team_color(int team) {
    return TEAM_COLORS[team % NUM_TEAM_COLORS];
}

/// Draw a rotated triangle outline.
void draw_rotated_triangle(SDL_Renderer* renderer,
                           float cx, float cy, float size,
                           float rotation,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // Triangle vertices relative to center, before rotation:
    // Top (nose): (0, -size)
    // Bottom-left: (-size, +size)
    // Bottom-right: (+size, +size)
    float cos_r = std::cos(rotation);
    float sin_r = std::sin(rotation);

    auto rotate = [&](float lx, float ly, float& ox, float& oy) {
        ox = cx + lx * cos_r - ly * sin_r;
        oy = cy + lx * sin_r + ly * cos_r;
    };

    float x0, y0, x1, y1, x2, y2;
    rotate(0.0f, -size, x0, y0);
    rotate(-size, size, x1, y1);
    rotate(size, size, x2, y2);

    SDL_SetRenderDrawColor(renderer, r, g, b, a);

    // Draw 3 lines forming the triangle
    SDL_RenderDrawLineF(renderer, x0, y0, x1, y1);
    SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
    SDL_RenderDrawLineF(renderer, x2, y2, x0, y0);
}

/// Draw a filled circle (approximated with radial lines).
void draw_filled_circle(SDL_Renderer* renderer,
                        float cx, float cy, float radius,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    int ir = static_cast<int>(radius);
    for (int dy = -ir; dy <= ir; ++dy) {
        int dx = static_cast<int>(std::sqrt(static_cast<float>(ir * ir - dy * dy)));
        SDL_RenderDrawLine(renderer,
            static_cast<int>(cx) - dx, static_cast<int>(cy) + dy,
            static_cast<int>(cx) + dx, static_cast<int>(cy) + dy);
    }
}

/// Draw a circle outline.
void draw_circle_outline(SDL_Renderer* renderer,
                         float cx, float cy, float radius,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    int ir = static_cast<int>(radius);
    // Midpoint circle algorithm
    int x = ir, y = 0;
    int icx = static_cast<int>(cx), icy = static_cast<int>(cy);
    int err = 1 - ir;
    while (x >= y) {
        SDL_RenderDrawPoint(renderer, icx + x, icy + y);
        SDL_RenderDrawPoint(renderer, icx - x, icy + y);
        SDL_RenderDrawPoint(renderer, icx + x, icy - y);
        SDL_RenderDrawPoint(renderer, icx - x, icy - y);
        SDL_RenderDrawPoint(renderer, icx + y, icy + x);
        SDL_RenderDrawPoint(renderer, icx - y, icy + x);
        SDL_RenderDrawPoint(renderer, icx + y, icy - x);
        SDL_RenderDrawPoint(renderer, icx - y, icy - x);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ArenaGameView::ArenaGameView(SDL_Renderer* sdl_renderer)
    : renderer_(sdl_renderer) {}

void ArenaGameView::set_bounds(int x, int y, int w, int h) noexcept {
    x_ = x;
    y_ = y;
    w_ = w;
    h_ = h;
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void ArenaGameView::render(const ArenaSession& arena, const Camera& camera,
                           int follow_index,
                           const std::vector<int>& team_assignments) {
    // Clip to viewport bounds
    SDL_Rect clip = {x_, y_, w_, h_};
    SDL_RenderSetClipRect(renderer_, &clip);

    render_background();
    render_towers(arena.towers(), camera);
    render_tokens(arena.tokens(), camera);
    render_bullets(arena.bullets(), camera);
    render_ships(arena.ships(), camera, follow_index, team_assignments);

    // Follow indicator on the selected ship
    if (follow_index >= 0
        && static_cast<std::size_t>(follow_index) < arena.ships().size()
        && arena.ships()[static_cast<std::size_t>(follow_index)].alive) {
        render_follow_indicator(
            arena.ships()[static_cast<std::size_t>(follow_index)], camera);
    }

    // Clear clip
    SDL_RenderSetClipRect(renderer_, nullptr);
}

// ---------------------------------------------------------------------------
// Background
// ---------------------------------------------------------------------------

void ArenaGameView::render_background() {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 20, 255);
    SDL_Rect bg = {x_, y_, w_, h_};
    SDL_RenderFillRect(renderer_, &bg);
}

// ---------------------------------------------------------------------------
// Towers (gray filled circles)
// ---------------------------------------------------------------------------

void ArenaGameView::render_towers(const std::vector<Tower>& towers,
                                  const Camera& camera) {
    for (const auto& tower : towers) {
        if (!tower.alive) continue;

        auto [sx, sy] = camera.world_to_screen(tower.x, tower.y, w_, h_);
        float sr = tower.radius * camera.zoom;

        // Off-screen culling
        float screen_x = sx + static_cast<float>(x_);
        float screen_y = sy + static_cast<float>(y_);
        if (screen_x + sr < static_cast<float>(x_) ||
            screen_x - sr > static_cast<float>(x_ + w_) ||
            screen_y + sr < static_cast<float>(y_) ||
            screen_y - sr > static_cast<float>(y_ + h_)) {
            continue;
        }

        draw_filled_circle(renderer_, screen_x, screen_y, sr,
                           120, 120, 130, 200);
    }
}

// ---------------------------------------------------------------------------
// Tokens (gold filled circles)
// ---------------------------------------------------------------------------

void ArenaGameView::render_tokens(const std::vector<Token>& tokens,
                                  const Camera& camera) {
    for (const auto& token : tokens) {
        if (!token.alive) continue;

        auto [sx, sy] = camera.world_to_screen(token.x, token.y, w_, h_);
        float sr = token.radius * camera.zoom;

        float screen_x = sx + static_cast<float>(x_);
        float screen_y = sy + static_cast<float>(y_);
        if (screen_x + sr < static_cast<float>(x_) ||
            screen_x - sr > static_cast<float>(x_ + w_) ||
            screen_y + sr < static_cast<float>(y_) ||
            screen_y - sr > static_cast<float>(y_ + h_)) {
            continue;
        }

        draw_filled_circle(renderer_, screen_x, screen_y, sr,
                           255, 200, 50, 220);
    }
}

// ---------------------------------------------------------------------------
// Ships (rotated colored triangles per team)
// ---------------------------------------------------------------------------

void ArenaGameView::render_ships(const std::vector<Triangle>& ships,
                                 const Camera& camera,
                                 int follow_index,
                                 const std::vector<int>& team_assignments) {
    float ship_screen_size = Triangle::SIZE * camera.zoom;

    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (!ships[i].alive) continue;

        auto [sx, sy] = camera.world_to_screen(ships[i].x, ships[i].y, w_, h_);
        float screen_x = sx + static_cast<float>(x_);
        float screen_y = sy + static_cast<float>(y_);

        // Off-screen culling
        if (screen_x + ship_screen_size < static_cast<float>(x_) ||
            screen_x - ship_screen_size > static_cast<float>(x_ + w_) ||
            screen_y + ship_screen_size < static_cast<float>(y_) ||
            screen_y - ship_screen_size > static_cast<float>(y_ + h_)) {
            continue;
        }

        int team = (i < team_assignments.size())
            ? team_assignments[i] : 0;
        const auto& color = team_color(team);

        bool is_focused = (static_cast<int>(i) == follow_index);
        uint8_t alpha = is_focused ? 255 : 100;

        draw_rotated_triangle(renderer_, screen_x, screen_y,
                              ship_screen_size, ships[i].rotation,
                              color.r, color.g, color.b, alpha);
    }
}

// ---------------------------------------------------------------------------
// Bullets (yellow rectangles)
// ---------------------------------------------------------------------------

void ArenaGameView::render_bullets(const std::vector<Bullet>& bullets,
                                   const Camera& camera) {
    float bullet_size = std::max(2.0f, 3.0f * camera.zoom);

    for (const auto& bullet : bullets) {
        if (!bullet.alive) continue;

        auto [sx, sy] = camera.world_to_screen(bullet.x, bullet.y, w_, h_);
        float screen_x = sx + static_cast<float>(x_);
        float screen_y = sy + static_cast<float>(y_);

        // Off-screen culling
        if (screen_x < static_cast<float>(x_) - bullet_size ||
            screen_x > static_cast<float>(x_ + w_) + bullet_size ||
            screen_y < static_cast<float>(y_) - bullet_size ||
            screen_y > static_cast<float>(y_ + h_) + bullet_size) {
            continue;
        }

        SDL_SetRenderDrawColor(renderer_, 255, 255, 80, 230);
        SDL_FRect rect = {
            screen_x - bullet_size / 2.0f,
            screen_y - bullet_size / 2.0f,
            bullet_size,
            bullet_size
        };
        SDL_RenderFillRectF(renderer_, &rect);
    }
}

// ---------------------------------------------------------------------------
// Follow indicator (white circle around followed ship)
// ---------------------------------------------------------------------------

void ArenaGameView::render_follow_indicator(const Triangle& ship,
                                            const Camera& camera) {
    auto [sx, sy] = camera.world_to_screen(ship.x, ship.y, w_, h_);
    float screen_x = sx + static_cast<float>(x_);
    float screen_y = sy + static_cast<float>(y_);
    float radius = (Triangle::SIZE + 6.0f) * camera.zoom;

    draw_circle_outline(renderer_, screen_x, screen_y, radius,
                        255, 255, 255, 180);
}

} // namespace neuroflyer
