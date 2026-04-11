#pragma once

#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ui/views/net_viewer_view.h>

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

namespace neuroflyer {

class TeamSkirmishScreen : public UIScreen {
public:
    TeamSkirmishScreen(TeamSkirmishConfig config, EvolutionConfig evo_config);
    ~TeamSkirmishScreen() override;

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "TeamSkirmish"; }

private:
    void initialize(AppState& state);
    bool handle_input(UIManager& ui);
    void evolve_generation();
    void render_world(Renderer& renderer);
    void render_hud(Renderer& renderer);

    TeamSkirmishConfig config_;
    EvolutionConfig evo_config_;

    ShipDesign ship_design_;  // from first team's fighter snapshot

    std::unique_ptr<TeamSkirmishSession> session_;
    std::vector<TeamPool> team_pools_;

    Camera camera_;
    enum class CameraMode { Swarm, Follow };
    CameraMode camera_mode_ = CameraMode::Swarm;

    bool initialized_ = false;
    bool paused_ = false;
    std::size_t generation_ = 1;
    int ticks_per_frame_ = 1;
    int selected_ship_ = -1;

    // Net viewer for follow mode
    enum class FollowNetView { Fighter, SquadLeader };
    FollowNetView follow_net_view_ = FollowNetView::Fighter;
    NetViewerViewState net_viewer_state_;

    std::mt19937 rng_;
};

} // namespace neuroflyer
