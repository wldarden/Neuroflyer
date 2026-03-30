# Arena Net Viewer — Design Spec

**Date:** 2026-03-30

## Overview

When the camera is following a fighter in arena mode, replace the right-panel info view with a live neural net viewer showing the followed fighter's brain. A small selector strip above the net viewer lets the user switch between viewing the **Fighter net** and the **Squad Leader net** for that fighter's team. When follow mode is disabled (arrow keys for free camera), the right panel reverts to the info view.

## Behavior

### Right Panel States

| Camera State | Right Panel Content |
|---|---|
| `camera_.following == true` | Net selector strip + live net viewer |
| `camera_.following == false` | `draw_arena_game_info_view()` (current behavior) |

Transition is immediate — arrow keys disable follow mode and the panel swaps back to info view. Tab to select a new ship re-enables follow mode and the net viewer.

### Net Selector Strip

A horizontal button pair rendered at the top of the right panel (~30px height):
- **"Fighter"** — shows the fighter net with `NetType::Fighter` labels/colors
- **"Squad Leader"** — shows the squad leader net with `NetType::SquadLeader` labels/colors

The active selection is visually highlighted. Default selection is Fighter.

### Live Activations

The net viewer displays real-time input values updated each tick. Input vectors are captured during `tick_arena()` for the currently followed ship:
- **Fighter net:** the full arena input vector (sensors + 6 squad leader inputs + memory) captured after `build_arena_ship_input()`
- **Squad Leader net:** the 14-value strategic input vector, captured after building the leader input in the per-team loop

When the followed ship dies mid-round, the last captured inputs remain displayed (frozen). The viewer continues showing the static net until the user selects another ship or follow mode is disabled.

## State Additions to ArenaGameScreen

### New Members

```cpp
// Net viewer
NetViewerViewState net_viewer_state_;
enum class FollowNetView { Fighter, SquadLeader };
FollowNetView follow_net_view_ = FollowNetView::Fighter;

// Live input capture (populated during tick_arena for the followed ship)
std::vector<float> last_fighter_input_;
std::vector<float> last_leader_input_;
```

### No Additional Compiled Networks Needed

The screen already maintains `fighter_nets_[team]` and `leader_nets_[team]` as compiled networks per team. The `NetViewerViewState` takes non-owning pointers to `Individual` and `Network`, so we point it at:
- **Fighter:** `team_population_[current_team_indices_[t]].fighter_individual` + `fighter_nets_[t]`
- **Squad Leader:** `team_population_[current_team_indices_[t]].squad_individual` + `leader_nets_[t]`

where `t = ship_teams_[selected_ship_]`.

## Input Capture in tick_arena()

### Fighter Input

In the per-fighter loop (after `build_arena_ship_input()`), add:

```cpp
if (camera_.following && i == static_cast<std::size_t>(selected_ship_)) {
    last_fighter_input_ = input;
}
```

### Squad Leader Input

In the per-team loop, the squad leader input is built inside `run_squad_leader()` which doesn't expose it. Two options:

**Option A:** Rebuild the 14-value input vector in `tick_arena()` after calling `run_squad_leader()`, using the same variables already in scope (`squad_health`, `home_heading_sin`, etc.). This duplicates ~3 lines but avoids changing the `run_squad_leader` API.

**Option B:** Add a `run_squad_leader` overload or variant that also returns the input vector.

**Choice: Option A.** The input is just a vector literal from values already computed in the same scope. No API changes needed.

```cpp
int followed_team = camera_.following ? ship_teams_[selected_ship_] : -1;
// ... after run_squad_leader for team t ...
if (static_cast<int>(t) == followed_team) {
    last_leader_input_ = { squad_health, home_heading_sin, home_heading_cos,
                           home_distance, home_health, squad_spacing_val,
                           cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                           ntm.active ? 1.0f : 0.0f,
                           ntm.active ? ntm.heading_sin : 0.0f,
                           ntm.active ? ntm.heading_cos : 0.0f,
                           ntm.active ? ntm.distance : 0.0f,
                           ntm.active ? ntm.threat_score : 0.0f };
}
```

Note: some variable names in the current `tick_arena()` scope differ slightly from what `run_squad_leader` uses internally (e.g., `stats.alive_fraction` vs `squad_health`). Use the same values passed to `run_squad_leader()` to ensure consistency.

## Rendering

### In render_arena() (ImGui Phase)

After the SDL game view and divider, conditionally render:

```
if (camera_.following && arena_ && selected_ship is valid and alive) {
    // Net selector strip
    render net selector buttons at top of right panel

    // Configure net viewer state
    look up team index from ship_teams_[selected_ship_]
    look up TeamIndividual from team_population_[current_team_indices_[team]]

    if (follow_net_view_ == Fighter) {
        net_viewer_state_.individual = &team.fighter_individual
        net_viewer_state_.network = &fighter_nets_[team]
        net_viewer_state_.ship_design = ship_design_
        net_viewer_state_.net_type = NetType::Fighter
        net_viewer_state_.input_values = last_fighter_input_
    } else {
        net_viewer_state_.individual = &team.squad_individual
        net_viewer_state_.network = &leader_nets_[team]
        net_viewer_state_.ship_design = ship_design_  // not used for SquadLeader labels but needed
        net_viewer_state_.net_type = NetType::SquadLeader
        net_viewer_state_.input_values = last_leader_input_
    }

    net_viewer_state_.render_x = info_x
    net_viewer_state_.render_y = selector_strip_bottom
    net_viewer_state_.render_w = right panel width
    net_viewer_state_.render_h = remaining height

    draw_net_viewer_view(net_viewer_state_, renderer)
} else {
    draw_arena_game_info_view(info)  // existing behavior
}
```

### In post_render() (SDL Phase)

```cpp
flush_net_viewer_view(net_viewer_state_, sdl_renderer);
```

### In on_exit() (Cleanup)

```cpp
destroy_net_viewer_view(net_viewer_state_);
```

## Edge Cases

- **Ship dies while followed:** Last captured inputs stay frozen. Net viewer continues showing the static net. User can Tab to select another alive ship.
- **Round ends / new match starts:** Networks are recompiled in `start_new_match()`. The non-owning pointers in `net_viewer_state_` will point at the new compiled networks after the next frame's render setup. Input vectors reset to empty until the first tick runs.
- **Speed multiplier (ticks_per_frame_ > 1):** Input capture overwrites each tick within the frame loop, so the viewer shows the last sub-tick's inputs. This is correct — it shows the most recent state.
- **Switching between Fighter/SquadLeader view:** The `NetViewerViewState` is reused. Scroll position resets naturally since the network topology changes.

## Files Modified

| File | Change |
|---|---|
| `include/neuroflyer/ui/screens/arena_game_screen.h` | Add `net_viewer_state_`, `follow_net_view_`, `last_fighter_input_`, `last_leader_input_` members; include `net_viewer_view.h` |
| `src/ui/screens/arena/arena_game_screen.cpp` | Capture inputs in `tick_arena()`, conditional render in `render_arena()`, flush in `post_render()`, cleanup in destructor/on_exit |

No new files needed.
