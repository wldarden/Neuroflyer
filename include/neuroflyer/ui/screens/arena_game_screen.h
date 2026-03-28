#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ui/ui_screen.h>

#include <memory>
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
    void render_arena(Renderer& renderer);

    ArenaConfig config_;
    std::unique_ptr<ArenaSession> arena_;
    Camera camera_;
    std::vector<Individual> population_;
    std::vector<neuralnet::Network> networks_;
    std::vector<std::vector<float>> recurrent_states_;
    EvolutionConfig evo_config_;
    ShipDesign ship_design_;
    bool initialized_ = false;
    std::size_t generation_ = 0;
    int ticks_per_frame_ = 1;
    std::size_t current_round_ = 0;
    std::vector<float> cumulative_scores_;
    int selected_ship_ = 0;
    bool paused_ = false;
};

} // namespace neuroflyer
