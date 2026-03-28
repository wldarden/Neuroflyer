#pragma once

#include <neuroflyer/ui/ui_view.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet/network.h>

namespace neuroflyer {

/// Reusable view that renders a neural network topology preview
/// using SDL deferred rendering. The owning screen sets the topology
/// and ship design to display, and calls on_draw() each frame.
class TopologyPreviewView : public UIView {
public:
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;

    /// Set what to display. Call before on_draw() when the data changes.
    void set_topology(const neuralnet::NetworkTopology* topology,
                      const ShipDesign* design);

    /// Clear the display (nothing to preview).
    void clear();

private:
    const neuralnet::NetworkTopology* topology_ = nullptr;
    const ShipDesign* design_ = nullptr;
};

} // namespace neuroflyer
