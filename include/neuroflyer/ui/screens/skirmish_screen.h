#pragma once

#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/skirmish_tournament.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ui/views/net_viewer_view.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace neuroflyer {

class SkirmishScreen : public UIScreen {
public:
    SkirmishScreen(Snapshot squad_snapshot,
                   Snapshot fighter_snapshot,
                   std::string genome_dir,
                   std::string variant_name);
    ~SkirmishScreen() override;

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "SquadSkirmish"; }

private:
    void initialize(AppState& state);
    bool handle_input(UIManager& ui);
    void evolve_generation();
    void render_world(Renderer& renderer);
    void render_hud(Renderer& renderer);

    Snapshot squad_snapshot_;
    Snapshot fighter_snapshot_;
    std::string genome_dir_;
    std::string variant_name_;
    ShipDesign ship_design_;

    SkirmishConfig config_;
    std::unique_ptr<SkirmishTournament> tournament_;

    EvolutionConfig evo_config_;
    std::vector<TeamIndividual> population_;

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

    std::mt19937 rng_;
};

} // namespace neuroflyer
