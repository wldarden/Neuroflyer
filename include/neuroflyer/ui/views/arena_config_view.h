#pragma once

#include <neuroflyer/arena_config.h>

namespace neuroflyer {

/// Draw arena configuration controls. Returns true when Start is clicked.
[[nodiscard]] bool draw_arena_config_view(ArenaConfig& config);

} // namespace neuroflyer
