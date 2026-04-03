#pragma once

#include <neuroflyer/skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/ui/ui_screen.h>

#include <string>

namespace neuroflyer {

class SkirmishConfigScreen : public UIScreen {
public:
    SkirmishConfigScreen(Snapshot squad_snapshot,
                         Snapshot fighter_snapshot,
                         std::string genome_dir,
                         std::string variant_name);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "SkirmishConfig"; }

private:
    SkirmishConfig config_;
    Snapshot squad_snapshot_;
    Snapshot fighter_snapshot_;
    std::string genome_dir_;
    std::string variant_name_;
};

} // namespace neuroflyer
