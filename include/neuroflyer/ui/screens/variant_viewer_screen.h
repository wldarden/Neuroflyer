#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/components/lineage_graph.h>
#include <neuroflyer/components/test_bench.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>

#include <neuralnet/network.h>

#include <string>
#include <vector>

namespace neuroflyer {

/// Net type tabs for the variant viewer.
enum class NetTypeTab { Fighters, SquadNets, Commander };

/// Filter applied to the Fighters tab variant list.
enum class FilterMode { All, SoloOnly, SquadOnly };

class VariantViewerScreen : public UIScreen {
public:
    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "VariantViewer"; }

private:
    // Sub-view dispatch
    enum class SubView { List, TestBench };
    SubView sub_view_ = SubView::List;

    // Net type tab
    NetTypeTab active_tab_ = NetTypeTab::Fighters;
    FilterMode fighter_filter_ = FilterMode::All;

    // Variant list state (fighters)
    struct VariantScreenState {
        std::vector<SnapshotHeader> variants;
        int selected_idx = -1;
        std::string genome_name;
        std::string genome_dir;
        bool show_lineage = false;
    };
    VariantScreenState vs_;
    std::string last_genome_;

    // Squad net state
    std::vector<SnapshotHeader> squad_variants_;
    int squad_selected_idx_ = 0;
    std::string paired_fighter_name_;

    // Create squad net modal state
    bool show_create_squad_modal_ = false;
    char squad_net_name_[64] = "";
    int squad_hidden_layers_ = 1;
    int squad_layer_sizes_[4] = {8, 4, 4, 4};

    // Pending modal results (set by callbacks, processed in on_draw)
    bool promote_pending_ = false;
    std::string promote_name_;
    bool delete_pending_ = false;
    std::string delete_variant_name_;
    bool squad_delete_pending_ = false;
    std::string squad_delete_name_;

    // Evolution settings cache
    EvolvableFlags evo_flags_;
    bool evo_loaded_ = false;

    // Sub-view component state (networks kept for test bench)
    std::vector<neuralnet::Network> networks_;
    TestBenchState test_bench_state_;
    LineageGraphState lineage_state_;

    // Actions from variant list
    enum class Action {
        Stay, Back, TrainFresh, TrainFrom, ArenaFresh, ArenaFrom,
        FighterDrill, AttackRuns,
        ViewNet, TestBench, PromoteToGenome, DeleteVariant,
        LineageTree,
        // Squad actions
        SquadTrainVsSquad, SquadTrainBaseAttack, SquadChangeFighter,
        SquadViewNet, SquadDelete, SquadSkirmish
    };

    // Internal draw helpers
    Action draw_variant_list(AppState& state, UIManager& ui);
    void draw_tab_bar();
    Action draw_squad_list(float content_h);
    Action draw_squad_actions(AppState& state, UIManager& ui, float content_h);

    // Helper: variant file path
    [[nodiscard]] std::string variant_path(const SnapshotHeader& hdr) const;
    [[nodiscard]] std::string squad_variant_path(const SnapshotHeader& hdr) const;
};

} // namespace neuroflyer
