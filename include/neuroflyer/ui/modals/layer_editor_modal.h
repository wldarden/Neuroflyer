#pragma once

#include <neuroflyer/ui/modals/editor_modal.h>
#include <neuroflyer/ui/modals/node_editor_modal.h>  // for NetEditorContext

#include <evolve/structured_genome.h>

namespace neuroflyer {

/// Modal for editing layer-wide properties (activation function for all nodes).
class LayerEditorModal : public EditorModal {
public:
    LayerEditorModal(int column, NetEditorContext ctx);

    [[nodiscard]] bool blocks_input() const override { return false; }

protected:
    void draw_form(AppState& state, UIManager& ui) override;
    void on_ok(UIManager& ui) override;
    void on_cancel(UIManager& ui) override;

private:
    int column_;
    NetEditorContext ctx_;
    evolve::StructuredGenome genome_backup_;
    bool modified_ = false;
};

} // namespace neuroflyer
