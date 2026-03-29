#pragma once

#include <neuroflyer/ui/ui_modal.h>
#include <neuroflyer/snapshot.h>

#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

/// Modal for selecting a fighter variant to pair with a squad net.
class FighterPairingModal : public UIModal {
public:
    FighterPairingModal(std::vector<SnapshotHeader> fighters,
                        std::function<void(const std::string&)> on_select);

    void on_draw(AppState& state, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "FighterPairingModal"; }

private:
    std::vector<SnapshotHeader> fighters_;
    std::function<void(const std::string&)> on_select_;
    int selected_idx_ = 0;
};

} // namespace neuroflyer
