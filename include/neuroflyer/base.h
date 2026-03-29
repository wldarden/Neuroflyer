#pragma once

#include <algorithm>

namespace neuroflyer {

struct Base {
    float x, y;
    float radius;
    float hp;
    float max_hp;
    int team_id;

    Base(float x, float y, float radius, float max_hp, int team_id)
        : x(x), y(y), radius(radius), hp(max_hp), max_hp(max_hp), team_id(team_id) {}

    void take_damage(float amount) {
        hp = std::max(0.0f, hp - amount);
    }

    [[nodiscard]] bool alive() const noexcept { return hp > 0.0f; }

    [[nodiscard]] float hp_normalized() const noexcept {
        return max_hp > 0.0f ? hp / max_hp : 0.0f;
    }
};

} // namespace neuroflyer
