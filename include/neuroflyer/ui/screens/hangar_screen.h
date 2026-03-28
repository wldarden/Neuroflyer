#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/components/test_bench.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet/network.h>

#include <string>
#include <vector>

namespace neuroflyer {

class HangarScreen : public UIScreen {
public:
    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "Hangar"; }

private:
    // Sub-view dispatch
    enum class SubView { GenomeList, TestBench, FitnessFunc };
    SubView sub_view_ = SubView::GenomeList;

    // Genome list state
    std::vector<GenomeInfo> genomes_;
    int selected_genome_idx_ = 0;
    bool show_delete_confirm_ = false;

    // Preview state
    struct PreviewState {
        int hovered_genome_idx = -1;
        int preview_x = 0, preview_y = 0, preview_w = 0, preview_h = 0;
        std::vector<neuralnet::NetworkTopology> genome_topologies;
        std::vector<ShipDesign> genome_designs;
        std::vector<std::string> genome_timestamps;
    };
    PreviewState preview_;

    // Sub-view component state (networks kept for test bench)
    std::vector<neuralnet::Network> networks_;
    TestBenchState test_bench_state_;

    // Actions from genome list
    enum class Action {
        Stay, Back, SelectGenome, CreateGenome, DeleteGenome,
        ViewNet, TestBench, FitnessFunc
    };

    // Internal draw helpers
    Action draw_genome_list(AppState& state, Renderer& renderer, UIManager& ui);
    void refresh_genomes(AppState& state);
};

} // namespace neuroflyer
