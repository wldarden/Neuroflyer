#pragma once

#include <neuroflyer/ui/ui_modal.h>

#include <functional>
#include <string>

namespace neuroflyer {

/// A yes/no confirmation dialog.
class ConfirmModal : public UIModal {
public:
    ConfirmModal(std::string title, std::string message,
                 std::function<void()> on_confirm);

    void on_draw(AppState& state, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "ConfirmModal"; }

private:
    std::string title_;
    std::string message_;
    std::function<void()> on_confirm_;
};

} // namespace neuroflyer
