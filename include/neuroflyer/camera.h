#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

namespace neuroflyer {

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
    bool following = true;
    int follow_index = 0;

    static constexpr float MIN_ZOOM = 0.05f;
    static constexpr float MAX_ZOOM = 4.0f;
    static constexpr float PAN_SPEED = 5.0f;

    [[nodiscard]] std::pair<float, float> world_to_screen(
        float wx, float wy, int viewport_w, int viewport_h) const {
        float sx = (wx - x) * zoom + static_cast<float>(viewport_w) / 2.0f;
        float sy = (wy - y) * zoom + static_cast<float>(viewport_h) / 2.0f;
        return {sx, sy};
    }

    [[nodiscard]] std::pair<float, float> screen_to_world(
        float sx, float sy, int viewport_w, int viewport_h) const {
        float wx = (sx - static_cast<float>(viewport_w) / 2.0f) / zoom + x;
        float wy = (sy - static_cast<float>(viewport_h) / 2.0f) / zoom + y;
        return {wx, wy};
    }

    void clamp_to_world(float world_w, float world_h,
                        int viewport_w, int viewport_h) {
        float half_vw = static_cast<float>(viewport_w) / (2.0f * zoom);
        float half_vh = static_cast<float>(viewport_h) / (2.0f * zoom);
        x = std::clamp(x, half_vw, world_w - half_vw);
        y = std::clamp(y, half_vh, world_h - half_vh);
    }

    void adjust_zoom(float delta) {
        zoom = std::clamp(zoom + delta, MIN_ZOOM, MAX_ZOOM);
    }
};

} // namespace neuroflyer
