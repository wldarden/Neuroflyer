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

class VariantViewerScreen : public UIScreen {
public:
    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "VariantViewer"; }

private:
    // Sub-view dispatch
    enum class SubView { List, TestBench };
    SubView sub_view_ = SubView::List;

    // Variant list state
    struct VariantScreenState {
        std::vector<SnapshotHeader> variants;
        int selected_idx = 0;
        std::string genome_name;
        std::string genome_dir;
        bool show_lineage = false;
    };
    VariantScreenState vs_;
    std::string last_genome_;
    // Pending modal results (set by callbacks, processed in on_draw)
    bool promote_pending_ = false;
    std::string promote_name_;
    bool delete_pending_ = false;
    std::string delete_variant_name_;

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
        ViewNet, TestBench, PromoteToGenome, DeleteVariant,
        LineageTree
    };

    // Internal draw helpers
    Action draw_variant_list(AppState& state, UIManager& ui);

    // Helper: variant file path
    [[nodiscard]] std::string variant_path(const SnapshotHeader& hdr) const;
};

} // namespace neuroflyer
