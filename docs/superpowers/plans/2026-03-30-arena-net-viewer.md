# Arena Net Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a live neural net viewer in the arena right panel when following a fighter, with a selector to switch between Fighter and Squad Leader nets.

**Architecture:** Conditional right-panel rendering in `ArenaGameScreen` — when `camera_.following` is true, replace the info view with a net selector strip + `NetViewerViewState`. Input vectors captured during `tick_arena()` feed live activations.

**Tech Stack:** C++17, ImGui, SDL2, existing `NetViewerViewState` / `draw_net_viewer_view` / `flush_net_viewer_view` infrastructure.

---

### Task 1: Add new state members to ArenaGameScreen header

**Files:**
- Modify: `include/neuroflyer/ui/screens/arena_game_screen.h`

- [ ] **Step 1: Add includes and members**

Add the `net_viewer_view.h` include and new members to the header:

```cpp
// After the existing includes (line 12, after #include <neuroflyer/ui/ui_screen.h>):
#include <neuroflyer/ui/views/net_viewer_view.h>
```

Add the enum before the class definition (inside the `neuroflyer` namespace, before `class ArenaGameScreen`):

```cpp
enum class FollowNetView { Fighter, SquadLeader };
```

Add these members at the end of the private section (after `Snapshot paired_fighter_snapshot_;` on line 69):

```cpp
    // Net viewer for follow mode
    NetViewerViewState net_viewer_state_;
    FollowNetView follow_net_view_ = FollowNetView::Fighter;
    std::vector<float> last_fighter_input_;
    std::vector<float> last_leader_input_;
```

- [ ] **Step 2: Build to verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
git add include/neuroflyer/ui/screens/arena_game_screen.h
git commit -m "feat(arena): add net viewer state members to ArenaGameScreen"
```

---

### Task 2: Capture live inputs during tick_arena()

**Files:**
- Modify: `src/ui/screens/arena/arena_game_screen.cpp` (tick_arena method, lines 221-336)

- [ ] **Step 1: Capture squad leader input for the followed team**

In `tick_arena()`, inside the per-team loop (after line 289 `team_orders[t] = run_squad_leader(...);`), add:

```cpp
        // Capture squad leader input for the followed ship's team
        if (camera_.following
            && selected_ship_ >= 0
            && static_cast<std::size_t>(selected_ship_) < arena_->ships().size()
            && ship_teams_[static_cast<std::size_t>(selected_ship_)] == static_cast<int>(t)) {
            last_leader_input_ = {
                stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp, stats.squad_spacing,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm.active ? 1.0f : 0.0f,
                ntm.active ? ntm.heading_sin : 0.0f,
                ntm.active ? ntm.heading_cos : 0.0f,
                ntm.active ? ntm.distance : 0.0f,
                ntm.active ? ntm.threat_score : 0.0f
            };
        }
```

- [ ] **Step 2: Capture fighter input for the followed ship**

In the per-fighter loop, after line 325 (`recurrent_states_[i])`), right after the `build_arena_ship_input` call and before the `forward` call (i.e., after line 325 but logically after line 320-325), add the capture after computing `input`:

Insert after line 325 (`recurrent_states_[i]);`) — actually, insert between the `build_arena_ship_input` call (lines 320-325) and the `forward` call (line 327):

```cpp
        // Capture fighter input for the followed ship
        if (camera_.following && i == static_cast<std::size_t>(selected_ship_)) {
            last_fighter_input_ = input;
        }
```

The resulting code should read:

```cpp
        auto input = build_arena_ship_input(
            ship_design_, ctx,
            sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
            sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
            sl_inputs.aggression, sl_inputs.spacing,
            recurrent_states_[i]);

        // Capture fighter input for the followed ship
        if (camera_.following && i == static_cast<std::size_t>(selected_ship_)) {
            last_fighter_input_ = input;
        }

        auto output = fighter_nets_[t].forward(input);
```

- [ ] **Step 3: Build to verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/ui/screens/arena/arena_game_screen.cpp
git commit -m "feat(arena): capture live fighter and squad leader inputs for net viewer"
```

---

### Task 3: Conditional right-panel rendering with net selector

**Files:**
- Modify: `src/ui/screens/arena/arena_game_screen.cpp` (render_arena method, lines 407-447)

- [ ] **Step 1: Replace the right panel rendering**

Replace the right panel section in `render_arena()`. The current code from line 407 to line 447 is:

```cpp
    // Right panel: ArenaGameInfoView (ImGui)
    int info_x = game_panel_w + 10;
    int info_w = renderer.net_w() - 20;
    (void)info_w; // used implicitly by ImGui window positioning

    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(static_cast<float>(renderer.net_w() - 20),
               static_cast<float>(game_panel_h - 20)),
        ImGuiCond_Always);
    ImGui::Begin("##ArenaInfo", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);

    ArenaInfoState info;
    info.generation = generation_;
    info.current_tick = arena_->current_tick();
    info.time_limit_ticks = config_.time_limit_ticks;
    info.alive_count = arena_->alive_count();
    info.total_count = config_.population_size();
    info.teams_alive = arena_->teams_alive();
    info.num_teams = config_.num_teams;

    // Aggregate per-ship kills into per-team totals
    info.team_enemy_kills.assign(config_.num_teams, 0);
    info.team_ally_kills.assign(config_.num_teams, 0);
    const auto& ek = arena_->enemy_kills();
    const auto& ak = arena_->ally_kills();
    for (std::size_t i = 0; i < ek.size(); ++i) {
        auto t_idx = static_cast<std::size_t>(arena_->team_of(i));
        info.team_enemy_kills[t_idx] += ek[i];
        info.team_ally_kills[t_idx] += ak[i];
    }
    info.team_scores = arena_->get_scores();

    draw_arena_game_info_view(info);

    ImGui::End();
```

Replace it with:

```cpp
    // Right panel
    int info_x = game_panel_w + 10;
    int info_w = renderer.net_w() - 20;

    // Determine if we should show the net viewer (follow mode with valid alive ship)
    bool show_net_viewer = camera_.following
        && selected_ship_ >= 0
        && static_cast<std::size_t>(selected_ship_) < arena_->ships().size()
        && arena_->ships()[static_cast<std::size_t>(selected_ship_)].alive;

    if (show_net_viewer) {
        // Net selector strip
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(info_w), 36.0f),
                                 ImGuiCond_Always);
        ImGui::Begin("##NetSelector", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);

        bool is_fighter = (follow_net_view_ == FollowNetView::Fighter);
        if (is_fighter) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.3f, 1.0f));
        if (ImGui::Button("Fighter", ImVec2(info_w / 2.0f - 8.0f, 0))) {
            follow_net_view_ = FollowNetView::Fighter;
        }
        if (is_fighter) ImGui::PopStyleColor();

        ImGui::SameLine();

        bool is_leader = (follow_net_view_ == FollowNetView::SquadLeader);
        if (is_leader) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.4f, 0.2f, 1.0f));
        if (ImGui::Button("Squad Leader", ImVec2(info_w / 2.0f - 8.0f, 0))) {
            follow_net_view_ = FollowNetView::SquadLeader;
        }
        if (is_leader) ImGui::PopStyleColor();

        ImGui::End();

        // Configure net viewer state
        int sel = static_cast<int>(selected_ship_);
        int team = ship_teams_[static_cast<std::size_t>(sel)];
        auto t = static_cast<std::size_t>(team);
        auto team_genome_idx = current_team_indices_[t];

        if (follow_net_view_ == FollowNetView::Fighter) {
            net_viewer_state_.individual = &team_population_[team_genome_idx].fighter_individual;
            net_viewer_state_.network = &fighter_nets_[t];
            net_viewer_state_.ship_design = ship_design_;
            net_viewer_state_.net_type = NetType::Fighter;
            net_viewer_state_.input_values = last_fighter_input_;
        } else {
            net_viewer_state_.individual = &team_population_[team_genome_idx].squad_individual;
            net_viewer_state_.network = &leader_nets_[t];
            net_viewer_state_.ship_design = ship_design_;
            net_viewer_state_.net_type = NetType::SquadLeader;
            net_viewer_state_.input_values = last_leader_input_;
        }

        int net_y = 50;  // below selector strip
        net_viewer_state_.render_x = info_x;
        net_viewer_state_.render_y = net_y;
        net_viewer_state_.render_w = info_w;
        net_viewer_state_.render_h = game_panel_h - net_y - 10;

        draw_net_viewer_view(net_viewer_state_, renderer.renderer_);
    } else {
        // Info view (default when not following)
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(static_cast<float>(info_w),
                   static_cast<float>(game_panel_h - 20)),
            ImGuiCond_Always);
        ImGui::Begin("##ArenaInfo", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);

        ArenaInfoState info;
        info.generation = generation_;
        info.current_tick = arena_->current_tick();
        info.time_limit_ticks = config_.time_limit_ticks;
        info.alive_count = arena_->alive_count();
        info.total_count = config_.population_size();
        info.teams_alive = arena_->teams_alive();
        info.num_teams = config_.num_teams;

        info.team_enemy_kills.assign(config_.num_teams, 0);
        info.team_ally_kills.assign(config_.num_teams, 0);
        const auto& ek = arena_->enemy_kills();
        const auto& ak = arena_->ally_kills();
        for (std::size_t i = 0; i < ek.size(); ++i) {
            auto t_idx = static_cast<std::size_t>(arena_->team_of(i));
            info.team_enemy_kills[t_idx] += ek[i];
            info.team_ally_kills[t_idx] += ak[i];
        }
        info.team_scores = arena_->get_scores();

        draw_arena_game_info_view(info);

        ImGui::End();
    }
```

- [ ] **Step 2: Build to verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/ui/screens/arena/arena_game_screen.cpp
git commit -m "feat(arena): conditional right panel with net selector and live net viewer"
```

---

### Task 4: Wire up post_render and cleanup

**Files:**
- Modify: `src/ui/screens/arena/arena_game_screen.cpp` (post_render method, line 527-529)

- [ ] **Step 1: Update post_render to flush the net viewer**

Replace the current `post_render` method (lines 527-529):

```cpp
void ArenaGameScreen::post_render(SDL_Renderer* /*sdl_renderer*/) {
    // No deferred SDL rendering needed for arena mode (no neural net panel)
}
```

With:

```cpp
void ArenaGameScreen::post_render(SDL_Renderer* sdl_renderer) {
    flush_net_viewer_view(net_viewer_state_, sdl_renderer);
}
```

- [ ] **Step 2: Add destructor for cleanup**

Add a destructor declaration to the header. In `include/neuroflyer/ui/screens/arena_game_screen.h`, after the constructor declaration (line 22):

```cpp
    ~ArenaGameScreen() override;
```

Add the destructor implementation in `src/ui/screens/arena/arena_game_screen.cpp`, after the constructor (after line 30):

```cpp
ArenaGameScreen::~ArenaGameScreen() {
    destroy_net_viewer_view(net_viewer_state_);
}
```

- [ ] **Step 3: Build to verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 4: Run tests**

Run: `./build/tests/neuroflyer_tests 2>&1 | tail -10`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/ui/screens/arena_game_screen.h src/ui/screens/arena/arena_game_screen.cpp
git commit -m "feat(arena): wire post_render flush and destructor cleanup for net viewer"
```

---

### Task 5: Visual smoke test

**Files:** None (manual testing)

- [ ] **Step 1: Build and run the app**

Run: `cmake --build build && ./build/neuroflyer`

- [ ] **Step 2: Test follow mode net viewer**

1. Start an arena session (create or select a genome, enter arena mode)
2. The camera starts in follow mode — verify the right panel shows the net selector strip with "Fighter" / "Squad Leader" buttons and a live neural net below
3. Verify the net shows live activations (input values changing each tick)
4. Click "Squad Leader" — verify the net switches to a different topology with 14 inputs and 5 outputs, yellow labels
5. Click "Fighter" — verify it switches back to the fighter net with sensor-based inputs

- [ ] **Step 3: Test panel swap on follow mode exit**

1. Press arrow keys to enter free camera mode
2. Verify the right panel immediately switches back to the info view (generation, time, kills, scores)
3. Press Tab to re-enter follow mode on a new ship
4. Verify the net viewer reappears

- [ ] **Step 4: Test edge cases**

1. Let the followed ship die — verify the net viewer stays showing the last state
2. Let a round end and new match start — verify no crash, net viewer continues working
3. Try different speed settings (1-4) — verify no visual glitches

- [ ] **Step 5: Commit any fixes if needed**
