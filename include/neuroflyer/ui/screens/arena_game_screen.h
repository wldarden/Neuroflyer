#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_match.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ui/ui_screen.h>

#include <memory>
#include <string>
#include <vector>

namespace neuroflyer {

class ArenaGameScreen : public UIScreen {
public:
    explicit ArenaGameScreen(const ArenaConfig& config);

    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "Arena"; }

private:
    void initialize(AppState& state);
    void handle_input(UIManager& ui);
    void tick_arena(AppState& state);
    void do_arena_evolution(AppState& state);
    void start_new_match(AppState& state);
    void render_arena(Renderer& renderer);

    ArenaConfig config_;
    std::unique_ptr<ArenaSession> arena_;
    Camera camera_;
    EvolutionConfig evo_config_;
    ShipDesign ship_design_;

    // Team-based evolution state
    std::vector<TeamIndividual> team_population_;
    NtmNetConfig ntm_config_;
    SquadLeaderNetConfig leader_config_;

    // Per-match compiled state (rebuilt each round)
    std::vector<neuralnet::Network> ntm_nets_;
    std::vector<neuralnet::Network> leader_nets_;
    std::vector<neuralnet::Network> fighter_nets_;
    std::vector<std::vector<float>> recurrent_states_;
    std::vector<int> ship_teams_;
    std::vector<float> team_fitness_;
    std::vector<std::size_t> current_team_indices_;

    bool initialized_ = false;
    std::size_t generation_ = 0;
    int ticks_per_frame_ = 1;
    std::size_t current_round_ = 0;
    int selected_ship_ = 0;
    bool paused_ = false;

    // Squad training mode: freeze fighters, evolve only squad nets
    bool squad_training_mode_ = false;
    bool base_attack_mode_ = false;
    std::string squad_paired_fighter_name_;
    std::string squad_genome_dir_;
    Snapshot paired_fighter_snapshot_;  // loaded once, reused to refreeze fighters
};

} // namespace neuroflyer
