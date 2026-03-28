#pragma once

#include <neuroflyer/ui/modals/editor_modal.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuralnet/network.h>

#include <evolve/structured_genome.h>

#include <vector>

namespace neuroflyer {

/// Context shared by node and layer editor modals.
struct NetEditorContext {
    Individual& individual;
    std::vector<neuralnet::Network>& networks;
    std::size_t best_idx;
    const ShipDesign& ship_design;
};

/// Modal for viewing/editing neural net node properties.
class NodeEditorModal : public EditorModal {
public:
    struct NodeRef {
        int column = -1;
        int node = -1;
    };

    NodeEditorModal(NodeRef node, NetEditorContext ctx);

    [[nodiscard]] bool blocks_input() const override { return false; }

protected:
    void draw_form(AppState& state, UIManager& ui) override;
    void on_ok(UIManager& ui) override;
    void on_cancel(UIManager& ui) override;

private:
    NodeRef node_;
    NetEditorContext ctx_;
    evolve::StructuredGenome genome_backup_;  // for cancel
    bool modified_ = false;
};

} // namespace neuroflyer
