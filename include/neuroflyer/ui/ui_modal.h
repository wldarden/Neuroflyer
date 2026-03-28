#pragma once

#include <neuroflyer/app_state.h>

namespace neuroflyer {

class UIManager;

class UIModal {
public:
    virtual ~UIModal() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_draw(AppState& state, UIManager& ui) = 0;
    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual bool blocks_input() const { return true; }
};

} // namespace neuroflyer
