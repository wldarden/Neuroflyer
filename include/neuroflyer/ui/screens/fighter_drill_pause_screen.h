#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/ui/ui_screen.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

class FighterDrillPauseScreen : public UIScreen {
public:
    FighterDrillPauseScreen(
        std::vector<Individual> population,
        std::size_t generation,
        ShipDesign ship_design,
        std::string genome_dir,
        std::string variant_name,
        EvolutionConfig evo_config,
        std::function<void(const EvolutionConfig&)> on_resume);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "FighterDrillPause"; }

private:
    std::vector<Individual> population_;
    std::size_t generation_;
    ShipDesign ship_design_;
    std::string genome_dir_;
    std::string variant_name_;
    EvolutionConfig evo_config_;
    std::function<void(const EvolutionConfig&)> on_resume_;

    // Tab state
    enum class Tab { Evolution, SaveVariants };
    Tab active_tab_ = Tab::Evolution;

    // Save variants tab state
    std::vector<std::size_t> sorted_indices_;
    std::vector<bool> selected_;
    bool indices_built_ = false;
};

} // namespace neuroflyer
