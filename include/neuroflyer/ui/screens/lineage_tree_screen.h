#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/components/lineage_graph.h>

#include <string>

namespace neuroflyer {

class LineageTreeScreen : public UIScreen {
public:
    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "LineageTree"; }

private:
    LineageGraphState lineage_state_;
    std::string last_genome_;
};

} // namespace neuroflyer
