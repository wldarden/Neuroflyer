#pragma once

#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/attack_run_session.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ui/views/net_viewer_view.h>

#include <neuralnet/network.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace neuroflyer {

class AttackRunScreen : public UIScreen {
public:
    AttackRunScreen(Snapshot source_snapshot,
                    std::string genome_dir,
                    std::string variant_name);
    ~AttackRunScreen() override;

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "AttackRuns"; }

private:
    void initialize(AppState& state);
    bool handle_input(UIManager& ui);
    void run_tick();
    void evolve_generation(AppState& state);
    void render_world(Renderer& renderer);
    void render_hud(Renderer& renderer);

    Snapshot source_snapshot_;
    std::string genome_dir_;
    std::string variant_name_;
    ShipDesign ship_design_;

    AttackRunConfig config_;
    std::unique_ptr<AttackRunSession> session_;

    EvolutionConfig evo_config_;
    std::vector<Individual> population_;
    std::vector<neuralnet::Network> nets_;
    std::vector<std::vector<float>> recurrent_states_;

    Camera camera_;
    enum class CameraMode { Swarm, Best, Worst };
    CameraMode camera_mode_ = CameraMode::Swarm;

    bool initialized_ = false;
    bool paused_ = false;
    std::size_t generation_ = 1;
    int ticks_per_frame_ = 1;
    int selected_ship_ = 0;

    NetViewerViewState net_viewer_state_;
    std::vector<float> last_input_;

    std::vector<int> drill_ship_teams_;
    std::mt19937 rng_;
};

} // namespace neuroflyer
