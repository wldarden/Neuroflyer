#pragma once

#include <neuroflyer/config.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>

#include <random>
#include <string>
#include <vector>

namespace neuroflyer {

enum class Screen {
    MainMenu,
    Hangar,
    CreateGenome,
    VariantViewer,
    LineageTree,
    Flying,
    PauseConfig,
};

struct GenStats {
    std::size_t generation = 0;
    float best = 0.0f;
    float avg = 0.0f;
    float stddev = 0.0f;
};

struct AppState {
    Screen current = Screen::MainMenu;
    Screen previous = Screen::MainMenu;

    // Paths
    std::string data_dir;
    std::string asset_dir;
    std::string settings_path;

    // Config (persisted)
    GameConfig config;

    // RNG
    std::mt19937 rng;

    // Navigation context
    std::string active_genome;
    std::string selected_variant;
    bool return_to_variant_view = false;
    std::string training_parent_name;

    // Pre-built population for fly session (set by variant viewer TrainFresh/TrainFrom)
    std::vector<Individual> pending_population;

    // ShipDesign to use with pending_population (sensors from the source variant)
    ShipDesign pending_ship_design;

    bool quit_requested = false;

    // Data invalidation flags — set by any screen that mutates data,
    // consumed by the screen that owns the cache.
    bool genomes_dirty = true;    // genome list changed (create/delete genome)
    bool variants_dirty = true;   // variant list changed (save/delete variant, promote)
    bool lineage_dirty = true;    // lineage tree changed (save/delete/promote)

    /// Mark all caches as dirty (e.g., after returning from training).
    void invalidate_all() {
        genomes_dirty = true;
        variants_dirty = true;
        lineage_dirty = true;
    }
};

inline void go_to_screen(AppState& state, Screen screen) {
    state.previous = state.current;
    state.current = screen;
}

} // namespace neuroflyer
