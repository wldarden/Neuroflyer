#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet/network.h>

#include <string>
#include <vector>

namespace neuroflyer {

class CreateGenomeScreen : public UIScreen {
public:
    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "CreateGenome"; }

private:
    // Form state (was static locals in draw_create_genome)
    char new_genome_name_[64] = "";
    int sight_rays_ = 8;
    int sensor_rays_ = 5;
    int memory_ = 4;
    int num_hidden_ = 2;
    int layer_sizes_[4] = {12, 12, 0, 0};
    int vision_type_ = 0;  // 0=Raycast, 1=Occulus

    // Evolvable parameter checkboxes
    bool evolve_sensor_angle_ = false;
    bool evolve_sensor_range_ = false;
    bool evolve_sensor_width_ = false;

    // Error message for invalid/duplicate genome names
    std::string error_message_;

    // Live neural-net topology preview
    struct PreviewState {
        int preview_x = 0;
        int preview_y = 0;
        int preview_w = 0;
        int preview_h = 0;
        neuralnet::NetworkTopology preview_topology;
        ShipDesign preview_design;
    };
    PreviewState preview_;
};

} // namespace neuroflyer
