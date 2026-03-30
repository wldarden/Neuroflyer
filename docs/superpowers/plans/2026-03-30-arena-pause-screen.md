# Arena Pause Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an arena mode pause screen with squad variant saving, pushed on Space from ArenaGameScreen.

**Architecture:** New `ArenaPauseScreen` subclass of `UIScreen`, following the same push/pop pattern as the scroller's `PauseConfigScreen`. Contains a team list sorted by fitness with multi-select checkboxes. Saves squad leader + NTM companion snapshots to `{genome_dir}/squad/` via existing `save_squad_variant()`.

**Tech Stack:** C++17, ImGui, SDL2, existing `save_squad_variant()`, `InputModal`, snapshot format v7.

---

### Task 1: Create ArenaPauseScreen header

**Files:**
- Create: `include/neuroflyer/ui/screens/arena_pause_screen.h`

- [ ] **Step 1: Create the header file**

```cpp
#pragma once

#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/ui/ui_screen.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

class ArenaPauseScreen : public UIScreen {
public:
    ArenaPauseScreen(
        std::vector<TeamIndividual> team_population,
        std::size_t generation,
        ShipDesign ship_design,
        std::string genome_dir,
        std::string paired_fighter_name,
        NtmNetConfig ntm_config,
        std::function<void()> on_resume);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "ArenaPause"; }

private:
    std::vector<TeamIndividual> team_population_;
    std::size_t generation_;
    ShipDesign ship_design_;
    std::string genome_dir_;
    std::string paired_fighter_name_;
    NtmNetConfig ntm_config_;
    std::function<void()> on_resume_;

    // Save tab state
    std::vector<std::size_t> sorted_indices_;
    std::vector<bool> selected_;
    bool indices_built_ = false;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Build to verify header compiles**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds (header not yet included anywhere).

- [ ] **Step 3: Commit**

```bash
git add include/neuroflyer/ui/screens/arena_pause_screen.h
git commit -m "feat(arena): add ArenaPauseScreen header"
```

---

### Task 2: Implement ArenaPauseScreen

**Files:**
- Create: `src/ui/screens/arena/arena_pause_screen.cpp`
- Modify: `CMakeLists.txt` (add new source file)

- [ ] **Step 1: Create the implementation file**

```cpp
#include <neuroflyer/ui/screens/arena_pause_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/modals/input_modal.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/snapshot.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>

namespace neuroflyer {

ArenaPauseScreen::ArenaPauseScreen(
    std::vector<TeamIndividual> team_population,
    std::size_t generation,
    ShipDesign ship_design,
    std::string genome_dir,
    std::string paired_fighter_name,
    NtmNetConfig ntm_config,
    std::function<void()> on_resume)
    : team_population_(std::move(team_population))
    , generation_(generation)
    , ship_design_(std::move(ship_design))
    , genome_dir_(std::move(genome_dir))
    , paired_fighter_name_(std::move(paired_fighter_name))
    , ntm_config_(std::move(ntm_config))
    , on_resume_(std::move(on_resume)) {}

void ArenaPauseScreen::on_draw(AppState& /*state*/, Renderer& renderer,
                                UIManager& ui) {
    // Build sorted indices on first draw
    if (!indices_built_) {
        sorted_indices_.resize(team_population_.size());
        for (std::size_t i = 0; i < sorted_indices_.size(); ++i)
            sorted_indices_[i] = i;

        std::sort(sorted_indices_.begin(), sorted_indices_.end(),
            [&](std::size_t a, std::size_t b) {
                return team_population_[a].fitness > team_population_[b].fitness;
            });

        selected_.assign(team_population_.size(), false);
        indices_built_ = true;
    }

    // Escape / Space: resume
    if (!ui.input_blocked() && (ImGui::IsKeyPressed(ImGuiKey_Escape)
            || ImGui::IsKeyPressed(ImGuiKey_Space))) {
        on_resume_();
        ui.pop_screen();
        return;
    }

    // Full-screen window
    float screen_w = static_cast<float>(renderer.screen_w());
    float screen_h = static_cast<float>(renderer.screen_h());
    ImGui::SetNextWindowPos(ImVec2(screen_w * 0.1f, screen_h * 0.1f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(screen_w * 0.8f, screen_h * 0.8f),
                             ImGuiCond_Always);
    ImGui::Begin("Arena — Save Squad Variants", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    if (team_population_.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
            "No team population.");
        ImGui::End();
        return;
    }

    float avail_h = ImGui::GetContentRegionAvail().y - 45.0f;
    float avail_w = ImGui::GetContentRegionAvail().x;
    float list_w = avail_w * 0.6f;
    float info_w = avail_w - list_w - 10.0f;

    // ---- Left: Team list ----
    ImGui::BeginChild("##TeamList", ImVec2(list_w, avail_h), true);

    // Select All / Clear
    {
        int sel_count = 0;
        for (bool s : selected_) if (s) ++sel_count;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d selected", sel_count);
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f), "%s", buf);
        ImGui::SameLine();
        if (ImGui::SmallButton("All")) {
            for (std::size_t i = 0; i < selected_.size(); ++i)
                selected_[i] = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            for (std::size_t i = 0; i < selected_.size(); ++i)
                selected_[i] = false;
        }
        ImGui::Separator();
    }

    for (std::size_t rank = 0; rank < sorted_indices_.size(); ++rank) {
        std::size_t pop_idx = sorted_indices_[rank];
        if (pop_idx >= team_population_.size()) continue;

        float fitness = team_population_[pop_idx].fitness;
        bool is_elite = rank < 2;  // top 2 teams highlighted

        char label[64];
        if (is_elite) {
            std::snprintf(label, sizeof(label), "Team #%zu (Elite)",
                rank + 1);
        } else {
            std::snprintf(label, sizeof(label), "Team #%zu", rank + 1);
        }

        ImGui::PushID(static_cast<int>(rank));

        bool is_selected = rank < selected_.size() && selected_[rank];
        if (ImGui::Selectable("##sel", is_selected,
                ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 36))) {
            if (rank < selected_.size())
                selected_[rank] = !selected_[rank];
        }

        ImGui::SameLine(5.0f);
        ImGui::BeginGroup();
        if (is_elite) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", label);
        } else {
            ImGui::Text("%s", label);
        }
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f),
            "Fitness: %d", static_cast<int>(fitness));
        ImGui::EndGroup();

        ImGui::PopID();
    }

    ImGui::EndChild();

    // ---- Right: Info panel ----
    ImGui::SameLine();
    ImGui::BeginChild("##InfoPanel", ImVec2(info_w, avail_h), true);

    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.9f, 1.0f), "Arena Training");
    ImGui::Separator();
    ImGui::Text("Generation: %zu", generation_);
    ImGui::Text("Teams: %zu", team_population_.size());
    if (!paired_fighter_name_.empty()) {
        ImGui::Text("Fighter: %s", paired_fighter_name_.c_str());
    }

    int sel_count = 0;
    for (bool s : selected_) if (s) ++sel_count;
    ImGui::Spacing();
    ImGui::Text("Selected: %d", sel_count);

    if (genome_dir_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
            "No genome directory.\nSaving disabled.");
    }

    ImGui::EndChild();

    // ---- Bottom bar ----
    ImGui::Separator();

    bool can_save = sel_count > 0 && !genome_dir_.empty();
    if (!can_save) ImGui::BeginDisabled();

    char btn_label[64];
    std::snprintf(btn_label, sizeof(btn_label), "Save %d Selected", sel_count);
    if (ImGui::Button(btn_label, ImVec2(180, 30))) {
        ui.push_modal(std::make_unique<InputModal>(
            "Save Squad Variants",
            "Enter base name:",
            [this](const std::string& base_name) {
                int saved = 0;
                int idx = 1;

                for (std::size_t rank = 0; rank < sorted_indices_.size(); ++rank) {
                    if (rank >= selected_.size() || !selected_[rank]) continue;
                    std::size_t pop_idx = sorted_indices_[rank];
                    if (pop_idx >= team_population_.size()) continue;

                    const auto& team = team_population_[pop_idx];
                    auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // Squad leader snapshot
                    {
                        Snapshot snap;
                        char snap_name[128];
                        std::snprintf(snap_name, sizeof(snap_name),
                            "%s-%d", base_name.c_str(), idx);
                        snap.name = snap_name;
                        snap.generation = static_cast<uint32_t>(generation_);
                        snap.created_timestamp = now_ts;
                        snap.ship_design = ship_design_;
                        snap.topology = team.squad_individual.topology;
                        snap.weights = team.squad_individual.genome.flatten("layer_");
                        snap.net_type = NetType::SquadLeader;
                        snap.paired_fighter_name = paired_fighter_name_;

                        // Sync per-node activations
                        for (std::size_t l = 0; l < snap.topology.layers.size(); ++l) {
                            std::string lp = "layer_" + std::to_string(l);
                            if (team.squad_individual.genome.has_gene(lp + "_activations")) {
                                const auto& ag = team.squad_individual.genome.gene(lp + "_activations");
                                auto& na = snap.topology.layers[l].node_activations;
                                na.resize(ag.values.size());
                                for (std::size_t n = 0; n < ag.values.size(); ++n) {
                                    int ai = std::clamp(
                                        static_cast<int>(std::round(ag.values[n])),
                                        0, neuralnet::ACTIVATION_COUNT - 1);
                                    na[n] = static_cast<neuralnet::Activation>(ai);
                                }
                            }
                        }

                        try {
                            save_squad_variant(genome_dir_, snap);
                        } catch (...) {}
                    }

                    // NTM companion snapshot
                    {
                        Snapshot ntm_snap;
                        char ntm_name[128];
                        std::snprintf(ntm_name, sizeof(ntm_name),
                            "%s-%d-ntm", base_name.c_str(), idx);
                        ntm_snap.name = ntm_name;
                        ntm_snap.generation = static_cast<uint32_t>(generation_);
                        ntm_snap.created_timestamp = now_ts;
                        ntm_snap.ship_design = ship_design_;
                        ntm_snap.topology = team.ntm_individual.topology;
                        ntm_snap.weights = team.ntm_individual.genome.flatten("layer_");
                        ntm_snap.net_type = NetType::Solo;

                        // Sync per-node activations
                        for (std::size_t l = 0; l < ntm_snap.topology.layers.size(); ++l) {
                            std::string lp = "layer_" + std::to_string(l);
                            if (team.ntm_individual.genome.has_gene(lp + "_activations")) {
                                const auto& ag = team.ntm_individual.genome.gene(lp + "_activations");
                                auto& na = ntm_snap.topology.layers[l].node_activations;
                                na.resize(ag.values.size());
                                for (std::size_t n = 0; n < ag.values.size(); ++n) {
                                    int ai = std::clamp(
                                        static_cast<int>(std::round(ag.values[n])),
                                        0, neuralnet::ACTIVATION_COUNT - 1);
                                    na[n] = static_cast<neuralnet::Activation>(ai);
                                }
                            }
                        }

                        try {
                            save_squad_variant(genome_dir_, ntm_snap);
                        } catch (...) {}
                    }

                    ++saved;
                    ++idx;
                }

                std::cout << "Saved " << saved
                          << " squad variants to " << genome_dir_ << "/squad/\n";
            },
            "gen" + std::to_string(generation_)));
    }

    if (!can_save) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Resume", ImVec2(120, 30))) {
        on_resume_();
        ui.pop_screen();
    }

    ImGui::End();
}

} // namespace neuroflyer
```

- [ ] **Step 2: Add source file to CMakeLists.txt**

In `CMakeLists.txt`, after the line `src/ui/screens/arena/arena_game_screen.cpp`, add:

```
    src/ui/screens/arena/arena_pause_screen.cpp
```

- [ ] **Step 3: Build to verify**

Run: `cmake --build build 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/ui/screens/arena/arena_pause_screen.cpp CMakeLists.txt
git commit -m "feat(arena): implement ArenaPauseScreen with squad variant saving"
```

---

### Task 3: Wire Space key in ArenaGameScreen to push ArenaPauseScreen

**Files:**
- Modify: `src/ui/screens/arena/arena_game_screen.cpp`

- [ ] **Step 1: Add include for the new screen**

At the top of `arena_game_screen.cpp`, after the existing includes (after `#include <neuroflyer/ui/screens/arena_game_screen.h>`), add:

```cpp
#include <neuroflyer/ui/screens/arena_pause_screen.h>
```

- [ ] **Step 2: Replace Space key handler**

In `handle_input()`, replace the Space handler (currently around line 217-219):

```cpp
    // Space: pause
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        paused_ = !paused_;
    }
```

With:

```cpp
    // Space: open pause screen
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        paused_ = true;
        ui.push_screen(std::make_unique<ArenaPauseScreen>(
            team_population_, generation_, ship_design_,
            squad_genome_dir_, squad_paired_fighter_name_, ntm_config_,
            [this]() { paused_ = false; }));
    }
```

Note: `handle_input` currently takes `UIManager& ui` — this is already the correct signature.

- [ ] **Step 3: Build to verify**

Run: `cmake --build build 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 4: Run tests**

Run: `./build/tests/neuroflyer_tests 2>&1 | tail -5`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/ui/screens/arena/arena_game_screen.cpp
git commit -m "feat(arena): wire Space key to push ArenaPauseScreen"
```

---

### Task 4: Visual smoke test

**Files:** None (manual testing)

- [ ] **Step 1: Build and run**

Run: `cmake --build build && ./build/neuroflyer`

- [ ] **Step 2: Test pause screen in squad training mode**

1. Create or select a genome with a variant
2. Start arena mode in squad training mode (this sets `squad_genome_dir_` and `squad_paired_fighter_name_`)
3. Let it run for a few generations
4. Press Space — verify the ArenaPauseScreen appears with team list sorted by fitness
5. Verify gold-highlighted elite teams at top
6. Select a few teams with checkboxes, verify selection count updates
7. Click "Save N Selected" — verify InputModal appears with pre-filled name
8. Enter a name and confirm — verify console prints "Saved N squad variants to ..."
9. Check `data/genomes/{name}/squad/` directory — verify `.bin` files exist
10. Press Escape — verify game resumes (unpaused)

- [ ] **Step 3: Test pause screen in regular arena mode**

1. Start arena mode without squad training
2. Press Space — verify pause screen appears
3. Verify save button is disabled (no genome directory)
4. Press Space or Escape — verify game resumes

- [ ] **Step 4: Commit any fixes if needed**
