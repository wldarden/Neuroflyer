#pragma once

#include <neuroflyer/ui/ui_modal.h>

#include <functional>
#include <string>

namespace neuroflyer {

/// A dialog with a text input field and OK/Cancel.
class InputModal : public UIModal {
public:
    InputModal(std::string title, std::string prompt,
               std::function<void(const std::string&)> on_submit,
               std::string default_value = "");

    void on_enter() override;
    void on_draw(AppState& state, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "InputModal"; }

private:
    std::string title_;
    std::string prompt_;
    std::function<void(const std::string&)> on_submit_;
    std::string default_value_;
    char buffer_[256] = {};
    bool focus_next_ = true;
};

} // namespace neuroflyer
