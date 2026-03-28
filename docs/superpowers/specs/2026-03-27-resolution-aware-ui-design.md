# Resolution-Aware UI — Design Spec

**Date:** 2026-03-27
**Status:** Approved
**Scope:** Fix stale dimension bugs so all screens/components handle window resize correctly

## Goal

Establish one rule for window dimensions: **query actual size every frame, never store it.** Fix the 3 problem areas (Renderer, FlySessionScreen, game-related views) to match the 9 screens that already do it correctly.

## The Rule

- **ImGui layout:** use `ImGui::GetIO().DisplaySize` — queried each frame, always current.
- **SDL draw calls:** use `SDL_GetRendererOutputSize()` — queried each call, always current.
- **Never cache dimensions** in member variables, state structs, or constants for layout purposes.
- **`SCREEN_W` / `SCREEN_H` constants** are only used as the default startup window size and GameConfig defaults. Never for runtime layout.

## Changes

### 1. Renderer: Replace stored dimensions with live queries

Remove `screen_h_`, `game_w_`, `net_w_` as stored member variables. Replace with functions:

```cpp
// renderer.h
[[nodiscard]] int screen_w() const;  // SDL_GetRendererOutputSize width
[[nodiscard]] int screen_h() const;  // SDL_GetRendererOutputSize height
[[nodiscard]] int game_w() const;    // screen_w() * 0.6
[[nodiscard]] int net_w() const;     // screen_w() - game_w()
```

Each calls `SDL_GetRendererOutputSize()` (the Renderer already holds `renderer_`). The 60/40 split is percentage-based, scales with any window size.

All callers that accessed `renderer.screen_h_` (public member) now call `renderer.screen_h()`. Same name pattern, but a function call that returns the current value.

Keep `renderer_` and `window_` as stored members — those don't change.

### 2. FlySessionScreen: Two kinds of dimensions

There are two distinct uses of game dimensions in the fly session:

**Rendering layout** (changes every frame): Where to draw the game panel, net panel, divider. These use `ImGui::GetIO().DisplaySize` each frame — `display.x * 0.6f` for game width, `display.y` for height. Passed to views via `set_bounds()`.

**Game-world dimensions** (fixed per generation): `GameSession` is constructed with `game_w` and `game_h` that define the physics world — ship clamping, tower/token spawn areas, collision boundaries. `build_ship_input()` also receives these for sensor normalization. These must be consistent for the entire generation or the AI's inputs become meaningless.

The fix: `FlySessionState` keeps `game_w` and `game_h` but they are set by querying the **current** window size at generation start (not hardcoded constants). They stay fixed for that generation's lifetime. If the window resizes mid-generation, rendering layout adapts immediately but the physics world dimensions stay as they were. Next generation picks up the new size.

```cpp
// At generation init (once per generation):
const ImVec2 display = ImGui::GetIO().DisplaySize;
s.game_w = display.x * 0.6f;
s.game_h = display.y;
// These are passed to GameSession constructor and build_ship_input()

// Each frame for rendering:
const ImVec2 display = ImGui::GetIO().DisplaySize;
float render_game_w = display.x * 0.6f;
float render_game_h = display.y;
// These are passed to set_bounds() and SDL draw calls
```

### 3. Views: Use passed bounds, not Renderer members

GameView, NetViewerView, and TestBenchView already support `set_bounds()` from the UIView base class. Their parent screens should pass current-frame bounds computed from `DisplaySize` each frame. The views should NOT reach into the Renderer for dimensions.

Where views need to make SDL draw calls (clipping, rendering), they use the bounds they were given, not `renderer.screen_h_`.

Note: The Renderer constructor currently calls `game_view.set_bounds()` once with stale values. This must be removed — the owning screen (FlySessionScreen) calls `set_bounds()` each frame with live dimensions instead.

### 4. Constants: Demote to defaults only

`constants.h` keeps `SCREEN_W = 1280` and `SCREEN_H = 800` but they are only referenced by:
- `main.cpp` for the initial `SDL_CreateWindow` call
- `GameConfig` default values for `window_width` / `window_height`

No screen or component references them for layout.

## What doesn't change

- 9 screens already using `ImGui::GetIO().DisplaySize` — untouched
- Menu background rendering — already uses `SDL_GetRendererOutputSize()`
- Settings screen apply/revert — already works
- UIManager set_resolution/on_resize infrastructure — stays as-is
- Save/load of graphics settings in GameConfig — unchanged

## Migration

For each file that currently reads `renderer.screen_h_`, `renderer.game_w_`, or `renderer.net_w_`:
1. Change to `renderer.screen_h()`, `renderer.game_w()`, `renderer.net_w()`
2. If the value was used to set ImGui layout, replace with `ImGui::GetIO().DisplaySize` instead
3. If the value was used for SDL clipping/drawing, the function call is fine

For FlySessionScreen:
1. Remove `game_w`/`game_h` from `FlySessionState`
2. Compute locally each frame
3. Pass to sub-views via `set_bounds()`
