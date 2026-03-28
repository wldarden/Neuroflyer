#include <neuroflyer/ui/views/game_view.h>

#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/ui/theme.h>

#include <neuralnet-ui/render_net_topology.h>
#include <neuralnet-ui/tiny_text.h>

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace neuroflyer {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

GameView::GameView(SDL_Renderer* sdl_renderer)
    : renderer_(sdl_renderer) {}

GameView::~GameView() {
    if (asteroid_tex_) SDL_DestroyTexture(asteroid_tex_);
    for (auto* tex : coin_frames_) {
        if (tex) SDL_DestroyTexture(tex);
    }
    for (auto* tex : star_textures_) {
        if (tex) SDL_DestroyTexture(tex);
    }
    for (auto& ship : ship_sprites_) {
        for (auto* tex : ship.frames) {
            if (tex) SDL_DestroyTexture(tex);
        }
    }
}

// ---------------------------------------------------------------------------
// Thin wrappers
// ---------------------------------------------------------------------------

void GameView::render_node(float x, float y, float radius, float activation,
                           uint8_t r, uint8_t g, uint8_t b) {
    neuralnet_ui::render_filled_circle(renderer_, x, y, radius, activation, r, g, b);
}

void GameView::draw_tiny_text(int x, int y, const char* text, int scale,
                              uint8_t r, uint8_t g, uint8_t b) {
    neuralnet_ui::draw_tiny_text(renderer_, x, y, text, scale, r, g, b);
}

// ---------------------------------------------------------------------------
// Game panel rendering
// ---------------------------------------------------------------------------

void GameView::render(const std::vector<GameSession>& sessions,
                      std::size_t focused_idx,
                      const std::vector<RayEndpoint>& focused_rays,
                      const RenderState& state,
                      const ShipDesign& ship_design) {
    // Clip to game panel bounds
    SDL_Rect clip = {x_, y_, w_, h_};
    SDL_RenderSetClipRect(renderer_, &clip);

    // Background starfield (drawn behind everything)
    float scroll = (focused_idx < sessions.size())
        ? sessions[focused_idx].scroll_offset() : 0.0f;
    render_starfield(scroll);

    // Render towers (from focused session's perspective)
    if (focused_idx < sessions.size()) {
        auto screen_towers = sessions[focused_idx].towers_in_screen_coords();
        for (const auto& tower : screen_towers) {
            if (asteroid_tex_) {
                int diameter = static_cast<int>(tower.radius * 2.0f);
                SDL_Rect dst = {
                    static_cast<int>(tower.x - tower.radius),
                    static_cast<int>(tower.y - tower.radius),
                    diameter, diameter
                };
                SDL_RenderCopyEx(renderer_, asteroid_tex_, nullptr, &dst,
                                 static_cast<double>(tower.rotation_angle),
                                 nullptr, SDL_FLIP_NONE);
            } else {
                render_node(tower.x, tower.y, tower.radius, 0.7f, 120, 80, 80);
            }
        }
    }

    // Render tokens (animated coins or gold circles)
    if (focused_idx < sessions.size()) {
        auto screen_tokens = sessions[focused_idx].tokens_in_screen_coords();
        if (!coin_frames_.empty()) {
            ++coin_anim_tick_;
            // Cycle through frames: advance every 3 render ticks for ~10fps at 30fps
            std::size_t frame = (coin_anim_tick_ / 3) % coin_frames_.size();
            for (const auto& token : screen_tokens) {
                int diameter = static_cast<int>(token.radius * 2.0f);
                SDL_Rect dst = {
                    static_cast<int>(token.x - token.radius),
                    static_cast<int>(token.y - token.radius),
                    diameter, diameter
                };
                SDL_RenderCopy(renderer_, coin_frames_[frame], nullptr, &dst);
            }
        } else {
            for (const auto& token : screen_tokens) {
                render_node(token.x, token.y, token.radius, 0.9f, 255, 200, 50);
            }
        }
    }

    // Render ships
    ++ship_anim_tick_;
    int ship_idx = std::clamp(state.ship_type, 0, NUM_SHIP_TYPES - 1);
    const auto& ship = ship_sprites_[ship_idx];
    bool has_ship_sprite = (ship.frames[0] != nullptr);
    int anim_frame = static_cast<int>((ship_anim_tick_ / 4) % SHIP_ANIM_FRAMES);

    for (std::size_t i = 0; i < sessions.size(); ++i) {
        const auto& sess = sessions[i];
        if (!sess.alive()) continue;

        const auto& tri = sess.triangle();
        bool is_focused = (i == focused_idx);

        if (state.view != ViewMode::Swarm && !is_focused) continue;

        uint8_t alpha = is_focused ? 255 : 60;

        if (has_ship_sprite && ship.frames[anim_frame]) {
            // Render ship sprite centered on triangle position
            // Scale to roughly 2x triangle size
            constexpr float SHIP_SCALE = 2.0f;
            int draw_w = static_cast<int>(Triangle::SIZE * SHIP_SCALE);
            int draw_h = static_cast<int>(Triangle::SIZE * SHIP_SCALE *
                static_cast<float>(ship.frame_h) / static_cast<float>(ship.frame_w));

            SDL_Rect dst = {
                static_cast<int>(tri.x) - draw_w / 2,
                static_cast<int>(tri.y) - draw_h / 2,
                draw_w, draw_h
            };

            SDL_SetTextureAlphaMod(ship.frames[anim_frame], alpha);
            SDL_RenderCopy(renderer_, ship.frames[anim_frame], nullptr, &dst);
        } else {
            // Fallback: draw triangle
            SDL_SetRenderDrawColor(renderer_, 0, 255, 136, alpha);
            int tx = static_cast<int>(tri.x);
            int ty = static_cast<int>(tri.y);
            int sz = static_cast<int>(Triangle::SIZE);
            SDL_RenderDrawLine(renderer_, tx, ty - sz, tx - sz, ty + sz);
            SDL_RenderDrawLine(renderer_, tx - sz, ty + sz, tx + sz, ty + sz);
            SDL_RenderDrawLine(renderer_, tx + sz, ty + sz, tx, ty - sz);
        }
    }

    // Reset alpha mod on ship textures
    if (has_ship_sprite && ship.frames[anim_frame]) {
        SDL_SetTextureAlphaMod(ship.frames[anim_frame], 255);
    }

    // Render vision system for focused individual
    if (focused_idx < sessions.size() && sessions[focused_idx].alive()) {
        const auto& tri = sessions[focused_idx].triangle();

        if (state.vision_type == 1) {
            // Occulus: overlapping oval fields
            auto oc_towers = sessions[focused_idx].towers_in_screen_coords();
            auto oc_tokens = sessions[focused_idx].tokens_in_screen_coords();
            render_occulus(ship_design, tri.x, tri.y, oc_towers, oc_tokens);
        } else {
            // Raycast: line-based vision
            for (std::size_t ri = 0; ri < focused_rays.size(); ++ri) {
                const auto& ray = focused_rays[ri];
                bool is_sensor = (ri == 2 || ri == 4 || ri == 6 || ri == 8 || ri == 10);

                uint8_t r = 0, g = 0, b = 0;
                if (ray.hit == HitType::Tower) {
                    r = 255; g = static_cast<uint8_t>(ray.distance * 80);
                } else if (ray.hit == HitType::Token) {
                    r = theme::token_color.r; g = theme::token_color.g; b = theme::token_color.b;
                } else if (is_sensor) {
                    r = theme::sensor_idle.r; g = theme::sensor_idle.g; b = theme::sensor_idle.b;
                } else {
                    g = static_cast<uint8_t>(100 + ray.distance * 100);
                }

                uint8_t alpha_val = is_sensor ? theme::ray_alpha_sensor : theme::ray_alpha_sight;
                SDL_SetRenderDrawColor(renderer_, r, g, b, alpha_val);
                SDL_RenderDrawLine(renderer_,
                    static_cast<int>(tri.x), static_cast<int>(tri.y),
                    static_cast<int>(ray.x), static_cast<int>(ray.y));
            }
        }
    }

    // Render bullets for focused session
    if (focused_idx < sessions.size()) {
        SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 255);
        for (const auto& b : sessions[focused_idx].bullets()) {
            if (!b.alive) continue;
            SDL_Rect br = {static_cast<int>(b.x) - 2, static_cast<int>(b.y) - 6, 4, 12};
            SDL_RenderFillRect(renderer_, &br);
        }
    }

    // Hero overlay: show when Best view is following a living ship but a dead one scored higher
    if (state.view == ViewMode::Best && state.hero_is_dead) {
        int glorious = static_cast<int>(state.glorious_hero_score);
        int living = static_cast<int>(state.living_hero_score);
        int diff = glorious - living;

        char buf[128];
        std::snprintf(buf, sizeof(buf), "GLORIOUS HERO: %d - LIVING HERO: %d = %d",
                      glorious, living, diff);
        draw_tiny_text(x_ + 8, y_ + 6, buf, 2, 255, 200, 80);
    }

    SDL_RenderSetClipRect(renderer_, nullptr);
}

// ---------------------------------------------------------------------------
// Starfield
// ---------------------------------------------------------------------------

void GameView::render_starfield(float scroll_offset) {
    if (star_textures_.empty()) return;

    // Initialize starfield on first call -- populate with random stars
    if (!starfield_initialized_) {
        starfield_initialized_ = true;

        std::mt19937 rng(42);  // deterministic seed for consistent starfield
        constexpr int NUM_STARS = 80;
        constexpr int NUM_COMMON = 5;   // star-00 through star-04
        constexpr int NUM_TOTAL = 42;

        std::uniform_real_distribution<float> x_dist(0.0f, static_cast<float>(w_));
        // Spread stars over a large Y range so they keep appearing as we scroll
        std::uniform_real_distribution<float> y_dist(-2000.0f, 50000.0f);
        std::uniform_real_distribution<float> scale_dist(0.04f, 0.25f);
        std::uniform_real_distribution<float> rare_chance(0.0f, 1.0f);
        std::uniform_int_distribution<int> common_idx(0, NUM_COMMON - 1);
        std::uniform_int_distribution<int> rare_idx(NUM_COMMON, NUM_TOTAL - 1);

        bg_stars_.reserve(NUM_STARS);
        for (int i = 0; i < NUM_STARS; ++i) {
            BgStar star{};
            star.x = x_dist(rng);
            star.y = y_dist(rng);
            star.scale = scale_dist(rng);

            // 98% common stars, 2% rare
            if (rare_chance(rng) < 0.02f) {
                star.sprite_idx = rare_idx(rng);
                star.scale *= 1.5f;  // rare objects are a bit bigger
            } else {
                star.sprite_idx = common_idx(rng);
            }

            // Parallax: smaller stars scroll slower (further away)
            // scale 0.04 -> parallax ~0.02, scale 0.25 -> parallax ~0.12
            star.parallax = star.scale * 0.5f;

            // Smaller/further stars are dimmer
            star.alpha = static_cast<uint8_t>(40 + star.scale * 600.0f);
            if (star.alpha < 40) star.alpha = 40;

            bg_stars_.push_back(star);
        }

        // Sort by parallax so distant stars draw first (behind closer ones)
        std::sort(bg_stars_.begin(), bg_stars_.end(),
            [](const BgStar& a, const BgStar& b) { return a.parallax < b.parallax; });
    }

    // Render each star with parallax scrolling
    float screen_h = static_cast<float>(h_);
    SDL_Rect clip = {x_, y_, w_, h_};
    SDL_RenderSetClipRect(renderer_, &clip);

    for (auto& star : bg_stars_) {
        // Parallax: star scrolls at a fraction of the main scroll speed
        float screen_y = screen_h - (star.y - scroll_offset * star.parallax);

        // Wrap: if star scrolled off bottom, move it above the screen
        // Use a large wrap range so the field feels infinite
        float wrap_range = 50000.0f * star.parallax + screen_h * 2.0f;
        while (screen_y > screen_h + 200.0f) screen_y -= wrap_range;
        while (screen_y < -200.0f) screen_y += wrap_range;

        if (screen_y < -150.0f || screen_y > screen_h + 150.0f) continue;

        auto idx = static_cast<std::size_t>(star.sprite_idx);
        if (idx >= star_textures_.size()) continue;

        // Scale the 219px source sprite
        int size = static_cast<int>(219.0f * star.scale);
        if (size < 2) size = 2;

        SDL_Rect dst = {
            x_ + static_cast<int>(star.x - static_cast<float>(size) / 2.0f),
            y_ + static_cast<int>(screen_y - static_cast<float>(size) / 2.0f),
            size, size
        };

        SDL_SetTextureAlphaMod(star_textures_[idx], star.alpha);
        SDL_RenderCopy(renderer_, star_textures_[idx], nullptr, &dst);
    }

    // Reset alpha mod
    for (auto* tex : star_textures_) {
        SDL_SetTextureAlphaMod(tex, 255);
    }

    SDL_RenderSetClipRect(renderer_, nullptr);
}

// ---------------------------------------------------------------------------
// Occulus vision rendering
// ---------------------------------------------------------------------------

void GameView::render_occulus(const ShipDesign& design, float ship_x, float ship_y,
                              const std::vector<Tower>& towers,
                              const std::vector<Token>& tokens) {
    constexpr float PI = std::numbers::pi_v<float>;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const auto& sensor : design.sensors) {
        if (sensor.type != SensorType::Occulus) continue;

        // Use compute_sensor_shape -- single source of truth shared with sensor engine
        auto shape = compute_sensor_shape(sensor, ship_x, ship_y);

        float cx = shape.center_x;
        float cy = shape.center_y;
        float cos_a = std::cos(shape.rotation);
        float sin_a = std::sin(shape.rotation);
        float maj = shape.major_radius;
        float min_r = shape.minor_radius;

        // Check if any tower or token overlaps this oval
        enum class FieldHit { None, Tower, Token };
        FieldHit hit = FieldHit::None;

        for (const auto& tower : towers) {
            if (!tower.alive) continue;
            float dx = tower.x - cx;
            float dy = tower.y - cy;
            float local_maj = dx * cos_a + dy * sin_a;
            float local_min = -dx * sin_a + dy * cos_a;
            // Expand ellipse by tower radius for overlap detection
            float eff_maj = maj + tower.radius;
            float eff_min = min_r + tower.radius;
            float val = (local_maj * local_maj) / (eff_maj * eff_maj) +
                       (local_min * local_min) / (eff_min * eff_min);
            if (val <= 1.0f) {
                hit = FieldHit::Tower;
                break;
            }
        }

        if (hit == FieldHit::None) {
            for (const auto& token : tokens) {
                if (!token.alive) continue;
                float dx = token.x - cx;
                float dy = token.y - cy;
                float local_maj = dx * cos_a + dy * sin_a;
                float local_min = -dx * sin_a + dy * cos_a;
                float eff_maj = maj + token.radius;
                float eff_min = min_r + token.radius;
                float val = (local_maj * local_maj) / (eff_maj * eff_maj) +
                           (local_min * local_min) / (eff_min * eff_min);
                if (val <= 1.0f) {
                    hit = FieldHit::Token;
                    break;
                }
            }
        }

        // Color based on hit state
        bool is_sensor = sensor.is_full_sensor;
        uint8_t r, g, b;
        uint8_t alpha;
        theme::Color tc;
        if (hit == FieldHit::Tower) {
            tc = is_sensor ? theme::sensor_danger : theme::sight_danger;
        } else if (hit == FieldHit::Token) {
            tc = is_sensor ? theme::sensor_token : theme::sight_token;
        } else {
            tc = is_sensor ? theme::sensor_idle : theme::sight_idle;
        }
        r = tc.r; g = tc.g; b = tc.b; alpha = tc.a;

        // Draw filled rotated ellipse using scanlines
        float bound = std::max(maj, min_r) + 2.0f;
        int ix_min = static_cast<int>(cx - bound);
        int ix_max = static_cast<int>(cx + bound);
        int iy_min = static_cast<int>(cy - bound);
        int iy_max = static_cast<int>(cy + bound);

        SDL_SetRenderDrawColor(renderer_, r, g, b, alpha);

        // Scanline fill: for each row, find the horizontal extent inside the ellipse
        for (int iy = iy_min; iy <= iy_max; ++iy) {
            float dy = static_cast<float>(iy) - cy;
            int left = ix_max + 1;
            int right = ix_min - 1;
            for (int ix = ix_min; ix <= ix_max; ++ix) {
                float dx = static_cast<float>(ix) - cx;
                // Rotate point into ellipse-local coords
                float local_major = dx * cos_a + dy * sin_a;
                float local_minor = -dx * sin_a + dy * cos_a;
                // Ellipse equation
                float val = (local_major * local_major) / (maj * maj) +
                           (local_minor * local_minor) / (min_r * min_r);
                if (val <= 1.0f) {
                    if (ix < left) left = ix;
                    if (ix > right) right = ix;
                }
            }
            if (left <= right) {
                SDL_RenderDrawLine(renderer_, left, iy, right, iy);
            }
        }

        // Draw oval outline slightly brighter
        SDL_SetRenderDrawColor(renderer_, r, g, b, static_cast<uint8_t>(std::min(alpha * 3, 255)));
        constexpr int OUTLINE_PTS = 48;
        float prev_ox = 0, prev_oy = 0;
        for (int i = 0; i <= OUTLINE_PTS; ++i) {
            float t = 2.0f * PI * static_cast<float>(i) / static_cast<float>(OUTLINE_PTS);
            // Point on unrotated ellipse
            float ex = maj * std::cos(t);
            float ey = min_r * std::sin(t);
            // Rotate
            float ox = cx + ex * cos_a - ey * sin_a;
            float oy = cy + ex * sin_a + ey * cos_a;
            if (i > 0) {
                SDL_RenderDrawLine(renderer_,
                    static_cast<int>(prev_ox), static_cast<int>(prev_oy),
                    static_cast<int>(ox), static_cast<int>(oy));
            }
            prev_ox = ox;
            prev_oy = oy;
        }
    }
}

// ---------------------------------------------------------------------------
// Ship preview
// ---------------------------------------------------------------------------

void GameView::render_ship_preview(int ship_type, int center_x, int center_y, int size) {
    int idx = std::clamp(ship_type, 0, NUM_SHIP_TYPES - 1);
    const auto& ship = ship_sprites_[idx];
    if (!ship.frames[0]) return;

    ++ship_anim_tick_;
    int frame = static_cast<int>((ship_anim_tick_ / 6) % SHIP_ANIM_FRAMES);

    // Maintain aspect ratio
    float aspect = static_cast<float>(ship.frame_h) / static_cast<float>(ship.frame_w);
    int draw_w = size;
    int draw_h = static_cast<int>(static_cast<float>(size) * aspect);

    SDL_Rect dst = {
        center_x - draw_w / 2,
        center_y - draw_h / 2,
        draw_w, draw_h
    };

    SDL_RenderCopy(renderer_, ship.frames[frame], nullptr, &dst);
}

// ---------------------------------------------------------------------------
// Asset loading
// ---------------------------------------------------------------------------

void GameView::load_asteroid_texture(const std::string& png_path) {
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "SDL_image init failed: " << IMG_GetError() << "\n";
        return;
    }

    SDL_Surface* surface = IMG_Load(png_path.c_str());
    if (!surface) {
        std::cerr << "Failed to load asteroid image: " << IMG_GetError() << "\n";
        return;
    }

    asteroid_tex_ = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);

    if (!asteroid_tex_) {
        std::cerr << "Failed to create asteroid texture: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetTextureBlendMode(asteroid_tex_, SDL_BLENDMODE_BLEND);
    std::cout << "Loaded asteroid texture from " << png_path << "\n";
}

void GameView::load_coin_strip(const std::string& png_path, int num_frames) {
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "SDL_image init failed: " << IMG_GetError() << "\n";
        return;
    }

    SDL_Surface* surface = IMG_Load(png_path.c_str());
    if (!surface) {
        std::cerr << "Failed to load coin strip: " << IMG_GetError() << "\n";
        return;
    }

    int frame_w = surface->w / num_frames;
    int frame_h = surface->h;
    coin_frame_size_ = frame_w;

    for (int i = 0; i < num_frames; ++i) {
        SDL_Rect src = {i * frame_w, 0, frame_w, frame_h};
        SDL_Surface* frame_surface = SDL_CreateRGBSurfaceWithFormat(
            0, frame_w, frame_h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!frame_surface) {
            std::cerr << "Failed to create surface: " << SDL_GetError() << "\n";
            continue;
        }
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(surface, &src, frame_surface, nullptr);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, frame_surface);
        SDL_FreeSurface(frame_surface);

        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            coin_frames_.push_back(tex);
        }
    }

    SDL_FreeSurface(surface);
    std::cout << "Loaded " << coin_frames_.size() << " coin frames from " << png_path << "\n";
}

void GameView::load_star_atlas(const std::string& png_path, int num_sprites) {
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) return;

    SDL_Surface* surface = IMG_Load(png_path.c_str());
    if (!surface) {
        std::cerr << "Failed to load star atlas: " << IMG_GetError() << "\n";
        return;
    }

    int cell = surface->h;  // square cells, height = cell size
    for (int i = 0; i < num_sprites; ++i) {
        SDL_Rect src = {i * cell, 0, cell, cell};
        SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormat(
            0, cell, cell, 32, SDL_PIXELFORMAT_RGBA32);
        if (!frame) {
            std::cerr << "Failed to create surface: " << SDL_GetError() << "\n";
            continue;
        }
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(surface, &src, frame, nullptr);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, frame);
        SDL_FreeSurface(frame);
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            star_textures_.push_back(tex);
        }
    }
    SDL_FreeSurface(surface);
    std::cout << "Loaded " << star_textures_.size() << " star sprites\n";
}

void GameView::load_ship_strip(const std::string& png_path, int num_frames) {
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) return;

    SDL_Surface* surface = IMG_Load(png_path.c_str());
    if (!surface) {
        std::cerr << "Failed to load ship strip: " << IMG_GetError() << "\n";
        return;
    }

    // Figure out which ship index this is from how many are loaded so far
    int ship_idx = -1;
    for (int i = 0; i < NUM_SHIP_TYPES; ++i) {
        if (ship_sprites_[i].frames[0] == nullptr) {
            ship_idx = i;
            break;
        }
    }
    if (ship_idx < 0) {
        SDL_FreeSurface(surface);
        return;
    }

    int frame_w = surface->w / num_frames;
    int frame_h = surface->h;
    ship_sprites_[ship_idx].frame_w = frame_w;
    ship_sprites_[ship_idx].frame_h = frame_h;

    for (int i = 0; i < num_frames && i < SHIP_ANIM_FRAMES; ++i) {
        SDL_Rect src = {i * frame_w, 0, frame_w, frame_h};
        SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormat(
            0, frame_w, frame_h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!frame) {
            std::cerr << "Failed to create surface: " << SDL_GetError() << "\n";
            continue;
        }
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(surface, &src, frame, nullptr);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, frame);
        SDL_FreeSurface(frame);
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            ship_sprites_[ship_idx].frames[i] = tex;
        }
    }

    SDL_FreeSurface(surface);
}

} // namespace neuroflyer
