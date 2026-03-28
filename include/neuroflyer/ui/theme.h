#pragma once

#include <cstdint>

namespace neuroflyer::theme {

/// RGBA color for SDL draw calls.
struct Color {
    uint8_t r, g, b, a;
};

// ============================================================
// Sensor colors — used by game view, test bench, input labels
// ============================================================

inline constexpr uint8_t sensor_idle_alpha = 100;
// Detail sensor (full: dist + danger + value + coin)
inline constexpr Color sensor_idle       = {160,  60, 220, sensor_idle_alpha};  // purple
inline constexpr Color sensor_danger     = {255,  40,  40, 160};  // red
inline constexpr Color sensor_token      = {255, 200,  50, 160};  // gold

// Sight sensor (distance only)
inline constexpr Color sight_idle        = {  0, 180, 100, sensor_idle_alpha};  // green
inline constexpr Color sight_danger      = {255,  40,  40, 130};  // red (dimmer)
inline constexpr Color sight_token       = {255, 200,  50, 130};  // gold (dimmer)

// Ray line alpha (for raycast mode visualization)
inline constexpr uint8_t ray_alpha_sensor = 160;
inline constexpr uint8_t ray_alpha_sight  = 120;

// ============================================================
// Sensor selection highlights (test bench)
// ============================================================

inline constexpr Color select_flash      = { 80, 255,  80, 120};  // green flash on select
inline constexpr Color select_deselect   = {255,  60,  60, 100};  // red on hover-to-deselect
inline constexpr Color select_outline    = {255, 200,  50, 220};  // gold outline on selected
inline constexpr Color hover_highlight   = {255, 200,  50,  80};  // gold on hover

// ============================================================
// Game objects
// ============================================================

inline constexpr Color token_color       = {255, 200,  50, 255};  // gold coins/tokens

// ============================================================
// Input label colors (test bench top bar + net panel)
// ============================================================

inline constexpr Color input_sensor      = {160,  90, 220, 255};  // purple for detail sensor inputs
inline constexpr Color input_sight       = {100, 130, 180, 255};  // blue-gray for sight inputs
inline constexpr Color input_active      = {255, 255, 100, 255};  // yellow when value < 1.0
inline constexpr Color input_sub_active  = {255, 200,  80, 255};  // sub-value active
inline constexpr Color input_sub_dim     = { 60,  60,  60, 255};  // sub-value inactive

// ============================================================
// Net viewer node colors (used by build_input_colors)
// ============================================================

inline constexpr Color node_sight        = { 60, 200,  80, 255};  // green for sight input nodes
inline constexpr Color node_sensor       = {160,  90, 220, 255};  // purple for detail sensor nodes
inline constexpr Color node_system       = { 68, 136, 255, 255};  // blue for pos_x, pos_y, speed
inline constexpr Color node_memory       = {220,  70,  70, 255};  // red for memory inputs

} // namespace neuroflyer::theme
