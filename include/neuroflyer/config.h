#pragma once

#include <cstddef>
#include <string>

namespace neuroflyer {

enum class GameMode { Scroller, Arena };

/// All runtime-tunable game parameters.
struct GameConfig {
    // Scoring
    float pts_per_distance = 0.1f;
    float pts_per_tower = 50.0f;
    float pts_per_token = 500.0f;
    float pts_per_bullet = -30.0f;  // cost (negative)

    // Population
    std::size_t population_size = 100;
    std::size_t elitism_count = 10;

    // Ship
    float ship_speed = 2.0f;
    int fire_cooldown = 30;
    int ship_type = 0;    // 0-9, selects ship sprite

    // World
    float scroll_speed = 2.0f;
    int starting_difficulty = 0;  // 0=normal, 1+=skip easy phase + N-1 ramp steps

    // Save system
    std::string active_genome;     // name of last-used genome directory (empty = none)
    int autosave_interval = 10;    // auto-save every N generations during training
    int mrca_memory_limit_mb = 64; // max MB for in-memory MRCA snapshot storage
    int mrca_prune_interval = 20;  // prune MRCA entries every N generations

    // Graphics
    int window_width = 1280;
    int window_height = 800;
    bool fullscreen = false;
    bool vsync = true;
    int max_fps = 60;

    // Position scoring multipliers
    float x_center_mult = 2.0f;   // Multiplier at horizontal center
    float x_edge_mult = 0.0f;     // Multiplier at horizontal edges
    float y_bottom_mult = 0.0f;   // Multiplier at bottom of screen
    float y_center_mult = 2.0f;   // Multiplier at vertical center
    float y_top_mult = 2.0f;      // Multiplier at top of screen

    /// Load config from JSON file. Returns defaults if file not found.
    static GameConfig load(const std::string& path);

    /// Save config to JSON file.
    void save(const std::string& path) const;
};

} // namespace neuroflyer
