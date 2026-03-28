# Resolution-Aware UI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix stale dimension bugs so all screens handle window resize correctly — query actual size every frame, never store it.

**Architecture:** Replace Renderer's stored `screen_h_`, `game_w_`, `net_w_` members with live-query functions. Fix FlySessionScreen to query window size at generation start (for physics) and each frame (for rendering). Fix test_bench to use live dimensions for SDL clipping.

**Tech Stack:** C++20, SDL2, Dear ImGui

**Spec:** `neuroflyer/docs/superpowers/specs/2026-03-27-resolution-aware-ui-design.md`

---

## Files to Modify

| File | Changes |
|------|---------|
| `neuroflyer/include/neuroflyer/renderer.h` | Replace stored dimension members with live-query functions |
| `neuroflyer/src/ui/renderer.cpp` | Implement live-query functions, update constructor and render() |
| `neuroflyer/src/ui/screens/game/fly_session_screen.cpp` | Query live dimensions at generation init + each frame for rendering |
| `neuroflyer/src/ui/views/test_bench.cpp` | Use live SDL query instead of stale `renderer.screen_h_` |
| `neuroflyer/src/ui/main.cpp` | Use saved config dimensions for initial window creation |

---

### Task 1: Renderer — Replace stored dimensions with live-query functions

**Files:**
- Modify: `neuroflyer/include/neuroflyer/renderer.h`
- Modify: `neuroflyer/src/ui/renderer.cpp`

- [ ] **Step 1: Update renderer.h**

Replace the stored dimension members with live-query functions. In `renderer.h`, change lines 51-53 from:

```cpp
    int screen_h_;
    int game_w_;   // left panel width (~60%)
    int net_w_;    // right panel width (~40%)
```

To:

```cpp
    /// Live-query dimensions — always current, never stale.
    [[nodiscard]] int screen_w() const;
    [[nodiscard]] int screen_h() const;
    [[nodiscard]] int game_w() const;   // screen_w() * 0.6
    [[nodiscard]] int net_w() const;    // screen_w() - game_w()
```

- [ ] **Step 2: Update renderer.cpp constructor**

Replace lines 19-28:

```cpp
Renderer::Renderer(SDL_Renderer* sdl_renderer, SDL_Window* sdl_window,
                   int screen_w, int screen_h)
    : game_view(sdl_renderer), renderer_(sdl_renderer), window_(sdl_window),
      screen_h_(screen_h) {
    game_w_ = static_cast<int>(screen_w * 0.6f);
    net_w_ = screen_w - game_w_;

    // Set default game view bounds to cover the left panel
    game_view.set_bounds(0, 0, game_w_, screen_h_);
}
```

With:

```cpp
Renderer::Renderer(SDL_Renderer* sdl_renderer, SDL_Window* sdl_window,
                   int /*screen_w*/, int /*screen_h*/)
    : game_view(sdl_renderer), renderer_(sdl_renderer), window_(sdl_window) {
}
```

The constructor parameters are kept for API compatibility but ignored — dimensions come from live queries now. Remove the `game_view.set_bounds()` call (the owning screen handles this per-frame).

- [ ] **Step 3: Implement live-query functions**

Add to `renderer.cpp`:

```cpp
int Renderer::screen_w() const {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    (void)h;
    return w;
}

int Renderer::screen_h() const {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    (void)w;
    return h;
}

int Renderer::game_w() const {
    return static_cast<int>(screen_w() * 0.6f);
}

int Renderer::net_w() const {
    return screen_w() - game_w();
}
```

- [ ] **Step 4: Update render() divider line**

In `renderer.cpp`, the `render()` method draws a divider line at line 55:

```cpp
SDL_RenderDrawLine(renderer_, game_w_, 0, game_w_, screen_h_);
```

Change to:

```cpp
SDL_RenderDrawLine(renderer_, game_w(), 0, game_w(), screen_h());
```

- [ ] **Step 5: Build**

Run: `cmake --build build --target neuroflyer --parallel`

This will likely produce compile errors in fly_session_screen.cpp and test_bench.cpp because they access `renderer.game_w_` etc. as public members. That's expected — Tasks 2 and 3 fix those.

- [ ] **Step 6: Commit (even if other files break — this task is self-contained in renderer)**

```bash
git add neuroflyer/include/neuroflyer/renderer.h neuroflyer/src/ui/renderer.cpp
git commit -m "refactor(neuroflyer): Renderer uses live-query dimensions instead of stored members"
```

---

### Task 2: FlySessionScreen — Live dimensions for rendering, generation-start for physics

**Files:**
- Modify: `neuroflyer/src/ui/screens/game/fly_session_screen.cpp`

- [ ] **Step 1: Remove SCREEN_W/SCREEN_H usage**

At lines 33-34, remove:
```cpp
using neuroflyer::SCREEN_W;
using neuroflyer::SCREEN_H;
```

- [ ] **Step 2: Fix game dimension initialization (line 225-227)**

Replace:
```cpp
    s.game_w = SCREEN_W * 0.6f;
    s.game_h = static_cast<float>(SCREEN_H);
```

With:
```cpp
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    s.game_w = display.x * 0.6f;
    s.game_h = display.y;
```

- [ ] **Step 3: Fix game dimension reset (lines 447-449)**

Same change — replace `SCREEN_W`/`SCREEN_H` with `ImGui::GetIO().DisplaySize`.

- [ ] **Step 4: Fix net viewer bounds (lines 353-356)**

Replace:
```cpp
    fly_net_state.render_x = renderer.game_w_ + 10;
    fly_net_state.render_y = 10;
    fly_net_state.render_w = renderer.net_w_ - 20;
    fly_net_state.render_h = renderer.screen_h_ - 20;
```

With:
```cpp
    fly_net_state.render_x = renderer.game_w() + 10;
    fly_net_state.render_y = 10;
    fly_net_state.render_w = renderer.net_w() - 20;
    fly_net_state.render_h = renderer.screen_h() - 20;
```

- [ ] **Step 5: Add per-frame game_view.set_bounds() before renderer.render()**

In the `render_fly()` function, just before the call to `renderer.render(...)` (around line 341-342), add:

```cpp
renderer.game_view.set_bounds(0, 0, renderer.game_w(), renderer.screen_h());
```

This replaces the one-time constructor call that was removed from the Renderer. The game view now gets current dimensions every frame.

Note: `FlySessionState` keeps `game_w`/`game_h` for physics (GameSession construction, sensor input normalization) — these are set from live dimensions at generation start and stay fixed for the generation. The `game_view.set_bounds()` call uses live rendering dimensions which may differ if the window resized mid-generation. This is correct: physics stays stable, rendering adapts.

- [ ] **Step 6: Build and verify**

Run: `cmake --build build --target neuroflyer --parallel`
Expected: Build succeeds (test_bench.cpp may still error — that's Task 3)

- [ ] **Step 7: Commit**

```bash
git add neuroflyer/src/ui/screens/game/fly_session_screen.cpp
git commit -m "refactor(neuroflyer): FlySessionScreen queries live window dimensions"
```

---

### Task 3: TestBench and remaining callers — Use live dimensions

**Files:**
- Modify: `neuroflyer/src/ui/views/test_bench.cpp`
- Possibly modify: any other file that references `renderer.screen_h_`, `renderer.game_w_`, or `renderer.net_w_`

- [ ] **Step 1: Fix test_bench.cpp (line 795)**

Replace:
```cpp
    int SCREEN_H = renderer.screen_h_;
```

With:
```cpp
    int screen_h = renderer.screen_h();
```

And update the clip rect on line 798:
```cpp
    SDL_Rect tb_clip = {0, 0, static_cast<int>(tb_game_w), screen_h};
```

Also, the test bench uses `renderer.game_view` for occulus rendering. Since the Renderer constructor no longer sets game_view bounds, add a `set_bounds()` call before any game_view usage in test_bench.cpp:

```cpp
renderer.game_view.set_bounds(0, 0, static_cast<int>(tb_game_w), screen_h);
```

Place this near the top of the SDL rendering section, before `renderer.game_view.render_occulus()` or `renderer.render_ship_preview()` calls.

- [ ] **Step 2: Search for any remaining references**

Run: `grep -rn 'renderer\.\(screen_h_\|game_w_\|net_w_\)' neuroflyer/src/`

Fix any remaining occurrences by changing `renderer.X_` to `renderer.X()`.

- [ ] **Step 3: Build all targets**

Run: `cmake --build build --target neuroflyer --parallel`
Expected: Clean build, zero warnings

- [ ] **Step 4: Commit**

```bash
git add neuroflyer/src/ui/views/test_bench.cpp
# (add any other modified files)
git commit -m "refactor(neuroflyer): fix remaining stale dimension references"
```

---

### Task 4: Verification & Smoke Test

- [ ] **Step 1: Grep for any remaining stale dimension references**

```bash
grep -rn 'renderer\.\(screen_h_\|game_w_\|net_w_\)' neuroflyer/src/
grep -rn 'SCREEN_W\|SCREEN_H' neuroflyer/src/ui/ --include='*.cpp'
```

First grep should return zero results. Second grep: only `main.cpp` should reference `SCREEN_W`/`SCREEN_H` (for initial window creation fallback).

- [ ] **Step 2: Run tests**

```bash
cmake --build build --target neuroflyer_tests --parallel
./build/neuroflyer/tests/neuroflyer_tests
```

All tests should pass — this is a pure rendering refactor, no logic changes.

- [ ] **Step 3: Manual smoke test**

1. Launch app → main menu background fills window correctly
2. Settings → change resolution → Apply → window resizes, menu background scales
3. Settings → fullscreen → Apply → fills screen, background scales
4. Settings → back to windowed → correct size restored
5. Fly → game panel (60%) and net panel (40%) scale to window width
6. Hangar → test bench → sensor visualization fits the game area correctly

- [ ] **Step 4: Final commit if cleanup needed**

```bash
git commit -m "chore(neuroflyer): resolution-aware UI cleanup and verification"
```
