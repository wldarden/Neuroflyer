#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/config.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/screens/analysis/analysis.h>
#include <neuroflyer/ship_design.h>

#include <cstddef>
#include <string>
#include <vector>

namespace neuroflyer {

/// UIScreen subclass for the pause/config overlay during a fly session.
///
/// Pushed on top of FlySessionScreen; popping returns to the running game.
class PauseConfigScreen : public UIScreen {
public:
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "PauseConfig"; }

private:
    void draw_save_variants_tab(AppState& state, Renderer& renderer,
                                UIManager& ui);

    /// Backups taken on first draw so Cancel can revert changes.
    GameConfig config_backup_;
    EvolutionConfig evo_config_backup_;
    EvolvableFlags evolvable_backup_;
    bool backup_saved_ = false;

    /// Analysis tab state (persists while pause screen is open).
    AnalysisState analysis_state_;

    /// Headless-run generation count.
    int headless_gens_ = 50;

    /// Save Variants tab state.
    std::vector<std::size_t> sorted_indices_;  ///< Population indices sorted by score
    std::vector<bool> selected_;               ///< Multi-select state per sorted index
    std::size_t last_pop_size_ = 0;            ///< Detect population changes
    std::size_t last_generation_ = ~std::size_t{0}; ///< Detect generation changes
    int hovered_sorted_idx_ = -1;              ///< Currently hovered row
    std::string save_error_;                   ///< Error message from last save attempt
};

} // namespace neuroflyer
