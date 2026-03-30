#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ui/views/net_designer_view.h>
#include <neuroflyer/ui/views/net_viewer_view.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>

#include <neuralnet/network.h>

#include <string>

namespace neuroflyer {

class VariantNetEditorScreen : public UIScreen {
public:
    VariantNetEditorScreen(Individual individual, neuralnet::Network network,
                           ShipDesign ship_design, std::string variant_path,
                           std::string variant_name,
                           NetType net_type = NetType::Solo);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "VariantNetEditor"; }

private:
    Individual individual_;
    std::vector<neuralnet::Network> networks_;  // single-element for editor context
    ShipDesign ship_design_;
    NetType net_type_ = NetType::Solo;
    std::string variant_path_;
    std::string variant_name_;

    NetDesignerState designer_state_;
    NetViewerViewState viewer_state_;

    void init_editor_from_topology();
};

} // namespace neuroflyer
