#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/ui_screen.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

class TeamSkirmishPauseScreen : public UIScreen {
public:
    TeamSkirmishPauseScreen(
        std::vector<TeamPool> team_pools,
        std::size_t generation,
        ShipDesign ship_design,
        EvolutionConfig evo_config,
        std::function<void(const EvolutionConfig&)> on_resume);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "TeamSkirmishPause"; }

private:
    void build_sorted_indices();

    std::vector<TeamPool> team_pools_;
    std::size_t generation_;
    ShipDesign ship_design_;
    EvolutionConfig evo_config_;
    std::function<void(const EvolutionConfig&)> on_resume_;

    enum class Tab { Evolution, SaveFighters, SaveSquadLeaders };
    Tab active_tab_ = Tab::Evolution;

    // Per-tab selection state
    int selected_team_fighters_ = 0;
    int selected_team_squads_ = 0;

    // Per-team sorted indices and selection for fighters
    std::vector<std::vector<std::size_t>> fighter_sorted_indices_;
    std::vector<std::vector<bool>> fighter_selected_;

    // Per-team sorted indices and selection for squad leaders
    std::vector<std::vector<std::size_t>> squad_sorted_indices_;
    std::vector<std::vector<bool>> squad_selected_;

    bool indices_built_ = false;
};

} // namespace neuroflyer
