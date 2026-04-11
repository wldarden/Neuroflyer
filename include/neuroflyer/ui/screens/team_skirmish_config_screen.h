#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/ui_screen.h>

#include <cstddef>
#include <string>
#include <vector>

namespace neuroflyer {

class TeamSkirmishConfigScreen : public UIScreen {
public:
    TeamSkirmishConfigScreen() = default;

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "TeamSkirmishConfig"; }

private:
    void refresh_genomes(AppState& state);

    // Team count and competition mode
    int num_teams_ = 2;
    CompetitionMode competition_mode_ = CompetitionMode::RoundRobin;

    // Per-team genome selections (indices into genome/variant lists)
    static constexpr int MAX_TEAMS = 8;
    int squad_genome_idx_[MAX_TEAMS] = {};
    int squad_variant_idx_[MAX_TEAMS] = {};
    int fighter_genome_idx_[MAX_TEAMS] = {};
    int fighter_variant_idx_[MAX_TEAMS] = {};

    // Loaded snapshots (updated when selections change)
    Snapshot squad_snapshots_[MAX_TEAMS];
    Snapshot fighter_snapshots_[MAX_TEAMS];
    bool snapshots_loaded_[MAX_TEAMS] = {};

    // Cached genome/variant lists
    struct GenomeEntry {
        std::string name;
        std::string dir;
    };
    struct VariantEntry {
        std::string name;
        std::string path;
    };

    std::vector<GenomeEntry> genomes_;
    // Per-genome variant lists (squad and fighter variants)
    std::vector<std::vector<VariantEntry>> squad_variants_;
    std::vector<std::vector<VariantEntry>> fighter_variants_;

    bool genomes_loaded_ = false;

    // Arena config (shared across all teams)
    SkirmishConfig arena_config_;

    // Evolution config
    EvolutionConfig evo_config_;
};

} // namespace neuroflyer
