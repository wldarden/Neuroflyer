#pragma once

#include <neuroflyer/config.h>

namespace neuroflyer {

/// Draw the fitness/scoring parameter editor panel.
/// Caller handles persistence (saving config to disk).
void draw_fitness_editor(GameConfig& config);

} // namespace neuroflyer
