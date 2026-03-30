#pragma once

#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/ui/ui_screen.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

class ArenaPauseScreen : public UIScreen {
public:
    ArenaPauseScreen(
        std::vector<TeamIndividual> team_population,
        std::size_t generation,
        ShipDesign ship_design,
        std::string genome_dir,
        std::string paired_fighter_name,
        NtmNetConfig ntm_config,
        std::function<void()> on_resume);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "ArenaPause"; }

private:
    std::vector<TeamIndividual> team_population_;
    std::size_t generation_;
    ShipDesign ship_design_;
    std::string genome_dir_;
    std::string paired_fighter_name_;
    NtmNetConfig ntm_config_;
    std::function<void()> on_resume_;

    // Save tab state
    std::vector<std::size_t> sorted_indices_;
    std::vector<bool> selected_;
    bool indices_built_ = false;
};

} // namespace neuroflyer
