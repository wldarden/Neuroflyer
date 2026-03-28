#pragma once

#include <neuroflyer/ui/ui_modal.h>

#include <string>

namespace neuroflyer {

/// Base class for modals that edit something with Ok/Cancel semantics.
/// Subclasses implement:
///   - draw_form() to render their specific editor controls
///   - on_ok() called when user confirms (optional — default is just close)
///   - on_cancel() called when user cancels (optional — default is just close)
///   - backup/restore logic in constructor and on_cancel()
class EditorModal : public UIModal {
public:
    explicit EditorModal(std::string title) : title_(std::move(title)) {}

    void on_draw(AppState& state, UIManager& ui) override;

    [[nodiscard]] const char* name() const override { return title_.c_str(); }

protected:
    virtual void draw_form(AppState& state, UIManager& ui) = 0;
    virtual void on_ok(UIManager& /*ui*/) {}
    virtual void on_cancel(UIManager& /*ui*/) {}

private:
    std::string title_;
};

} // namespace neuroflyer
