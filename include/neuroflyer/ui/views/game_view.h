#pragma once

#include <neuroflyer/config.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/game.h>
#include <neuroflyer/ray.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace neuroflyer {

enum class ViewMode { Swarm, Best, Worst };

struct RenderState {
    ViewMode view = ViewMode::Swarm;
    std::size_t generation = 0;
    std::size_t alive_count = 0;
    std::size_t total_count = 0;
    float best_score = 0.0f;
    // When Best view focuses on best living (not the all-time best this gen):
    float glorious_hero_score = 0.0f;  // best score (dead or alive)
    float living_hero_score = 0.0f;    // best living score
    bool hero_is_dead = false;         // true when the top scorer is dead
    int ship_type = 0;                 // which ship sprite to use
    int vision_type = 0;               // 0=Raycast, 1=Occulus
};

/// Owns all game-panel rendering: assets, animation state, starfield, and the
/// render() method that draws towers, tokens, ships, vision, and the hero
/// overlay into a rectangular region of the screen.
class GameView {
public:
    explicit GameView(SDL_Renderer* sdl_renderer);
    ~GameView();

    // Non-copyable (owns SDL textures)
    GameView(const GameView&) = delete;
    GameView& operator=(const GameView&) = delete;
    GameView(GameView&&) = delete;
    GameView& operator=(GameView&&) = delete;

    // --- Render bounds (set by owning screen before render) ----------------

    void set_bounds(int x, int y, int w, int h) noexcept {
        x_ = x; y_ = y; w_ = w; h_ = h;
    }

    [[nodiscard]] int x() const noexcept { return x_; }
    [[nodiscard]] int y() const noexcept { return y_; }
    [[nodiscard]] int w() const noexcept { return w_; }
    [[nodiscard]] int h() const noexcept { return h_; }

    // --- Asset loading -----------------------------------------------------

    void load_asteroid_texture(const std::string& png_path);
    void load_coin_strip(const std::string& png_path, int num_frames);
    void load_star_atlas(const std::string& png_path, int num_sprites);
    void load_ship_strip(const std::string& png_path, int num_frames);

    // --- Game panel rendering ----------------------------------------------

    /// Render the full game panel: starfield, towers, tokens, ships, vision,
    /// bullets, hero overlay. Uses the bounds set by set_bounds().
    void render(const std::vector<GameSession>& sessions,
                std::size_t focused_idx,
                const std::vector<RayEndpoint>& focused_rays,
                const RenderState& state,
                const ShipDesign& ship_design);

    /// Render scrolling starfield background (call before game objects).
    void render_starfield(float scroll_offset);

    /// Render occulus vision fields.
    void render_occulus(const ShipDesign& design, float ship_x, float ship_y,
                        const std::vector<Tower>& towers,
                        const std::vector<Token>& tokens);

    /// Render a large animated ship preview at the given center position.
    void render_ship_preview(int ship_type, int center_x, int center_y, int size);

    /// Draw tiny text using the neuralnet-ui bitmap font.
    void draw_tiny_text(int x, int y, const char* text, int scale,
                        uint8_t r, uint8_t g, uint8_t b);

    // --- Asset accessors (for test bench) ----------------------------------

    [[nodiscard]] SDL_Texture* asteroid_texture() const noexcept { return asteroid_tex_; }
    [[nodiscard]] const std::vector<SDL_Texture*>& coin_frames() const noexcept { return coin_frames_; }

    // --- SDL renderer access (for callers that need raw SDL) ---------------

    [[nodiscard]] SDL_Renderer* sdl_renderer() const noexcept { return renderer_; }

private:
    void render_node(float x, float y, float radius, float activation,
                     uint8_t r, uint8_t g, uint8_t b);

    SDL_Renderer* renderer_;

    // Render bounds
    int x_ = 0;
    int y_ = 0;
    int w_ = 0;
    int h_ = 0;

    // Asteroid
    SDL_Texture* asteroid_tex_ = nullptr;

    // Coin animation
    std::vector<SDL_Texture*> coin_frames_;
    int coin_frame_size_ = 0;
    uint32_t coin_anim_tick_ = 0;

    // Ships: ship_sprites_[ship_idx]
    static constexpr int NUM_SHIP_TYPES = 10;
    static constexpr int SHIP_ANIM_FRAMES = 4;
    struct ShipSprite {
        SDL_Texture* frames[4] = {};
        int frame_w = 0;
        int frame_h = 0;
    };
    ShipSprite ship_sprites_[10];
    uint32_t ship_anim_tick_ = 0;

    // Starfield
    std::vector<SDL_Texture*> star_textures_;
    struct BgStar {
        float x, y;
        int sprite_idx;
        float scale;
        float parallax;
        uint8_t alpha;
    };
    std::vector<BgStar> bg_stars_;
    bool starfield_initialized_ = false;
};

} // namespace neuroflyer
