#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/game.h>
#include <neuroflyer/mrca_tracker.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neuroflyer {

struct FlySessionState {
    bool initialized = false;

    enum class Phase { Running, Evolving, HeadlessRunning, HeadlessEvolving };
    Phase phase = Phase::Running;

    std::size_t generation = 0;
    int ticks_per_frame = 1;
    bool needs_reset = false;

    // Population
    std::vector<Individual> population;
    std::vector<GameSession> sessions;
    std::vector<neuralnet::Network> networks;
    std::vector<std::vector<float>> recurrent_states;
    std::vector<std::vector<float>> last_inputs;
    EvolutionConfig evo_config;

    // Input/output sizes
    std::size_t input_size = 0;
    std::size_t output_size = 5;
    std::size_t mem_slots = 0;

    // Tracking
    std::vector<GenStats> gen_history;
    std::vector<StructuralHistogram> structural_history;
    MrcaTracker mrca_tracker;

    // Rendering
    ViewMode view = ViewMode::Swarm;

    // Headless
    int headless_remaining = 0;
    int headless_total = 0;          ///< Total requested (for progress bar)
    bool headless_overlay_drawn = false; ///< True after overlay is presented, ready to run ticks
    std::vector<GenStats> headless_stats; ///< Stats collected during headless run

    // Level seed
    uint32_t level_seed = 0;

    // Game panel dimensions
    float game_w = 0.0f;
    float game_h = 0.0f;

    // Ship design for sensor engine
    ShipDesign ship_design;

    // Genome directory for autosave
    std::string active_genome_dir;

    // Default constructor — initializes MrcaTracker with placeholder values.
    // Real configuration is applied during initialization in draw_fly_session.
    FlySessionState();

    void reset();
};

FlySessionState& get_fly_session_state();

} // namespace neuroflyer
