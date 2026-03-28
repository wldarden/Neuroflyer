#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

namespace neuroflyer {

class UIManager;

class UIView {
public:
    virtual ~UIView() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_resize(int w, int h) { (void)w; (void)h; }
    virtual void on_draw(AppState& state, Renderer& renderer, UIManager& ui) = 0;

    void set_bounds(float x, float y, float w, float h) {
        x_ = x; y_ = y; w_ = w; h_ = h;
    }
    [[nodiscard]] float x() const { return x_; }
    [[nodiscard]] float y() const { return y_; }
    [[nodiscard]] float w() const { return w_; }
    [[nodiscard]] float h() const { return h_; }

protected:
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace neuroflyer
