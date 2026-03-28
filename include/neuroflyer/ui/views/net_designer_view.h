#pragma once

#include <neuroflyer/ui/ui_view.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuralnet/network.h>

#include <functional>

namespace neuroflyer {

struct NetDesignerState {
    int edit_num_hidden = 2;
    int edit_layer_sizes[8] = {12, 12, 8, 8, 4, 4, 4, 4};
    int edit_memory_slots = 4;
    int current_memory_slots = 0;  // display-only, from loaded network

    // Called when user clicks "Create Random Net"
    // Provides the generated individual and its built network
    std::function<void(Individual, neuralnet::Network)> on_create;
};

/// Draw the network designer controls within an existing ImGui child window.
/// Does NOT create its own ImGui::Begin/End.
void draw_net_designer(NetDesignerState& state, const ShipDesign& ship_design,
                       std::size_t population_size);

} // namespace neuroflyer
