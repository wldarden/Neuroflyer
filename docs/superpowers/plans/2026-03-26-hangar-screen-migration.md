# Hangar Screen Migration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the Hangar screen from a `draw_hangar()` free function with static locals to a `HangarScreen : UIScreen` subclass with member state. Navigation uses UIManager push/pop instead of go_to_screen().

**Architecture:** HangarScreen owns all state that was previously static (genome list, selection, preview, sub-view mode). Helper functions become private methods. Sub-view components (NetViewer, TestBench) remain as existing components for now — they get their own UIView migration later. MainMenuScreen pushes HangarScreen directly instead of wrapping draw_hangar in LegacyScreen.

**Tech Stack:** C++20, SDL2, ImGui, CMake

---

### Task 1: Create HangarScreen header

**Files:**
- Create: `neuroflyer/include/neuroflyer/ui/screens/hangar_screen.h`

- [ ] **Step 1: Create the header**

Create `neuroflyer/include/neuroflyer/ui/screens/hangar_screen.h`:

```cpp
#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/components/net_viewer.h>
#include <neuroflyer/components/test_bench.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet/network.h>

#include <string>
#include <vector>

namespace neuroflyer {

class HangarScreen : public UIScreen {
public:
    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "Hangar"; }

private:
    // Sub-view dispatch
    enum class SubView { GenomeList, NetViewer, TestBench, FitnessFunc };
    SubView sub_view_ = SubView::GenomeList;

    // Genome list state
    std::vector<GenomeInfo> genomes_;
    int selected_genome_idx_ = 0;
    bool show_delete_confirm_ = false;

    // Preview state
    struct PreviewState {
        int hovered_genome_idx = -1;
        int preview_x = 0, preview_y = 0, preview_w = 0, preview_h = 0;
        std::vector<neuralnet::NetworkTopology> genome_topologies;
        std::vector<ShipDesign> genome_designs;
        std::vector<std::string> genome_timestamps;
    };
    PreviewState preview_;

    // Sub-view component state
    NetViewerState net_viewer_state_;
    TestBenchState test_bench_state_;

    // Actions from genome list
    enum class Action {
        Stay, Back, SelectGenome, CreateGenome, DeleteGenome,
        ViewNet, TestBench, FitnessFunc
    };

    // Internal draw helpers
    Action draw_genome_list(AppState& state, Renderer& renderer, UIManager& ui);
    void refresh_genomes(AppState& state);
};

} // namespace neuroflyer
```

- [ ] **Step 2: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/screens/hangar_screen.h && \
git commit -m "feat(neuroflyer): HangarScreen header with member state"
```

---

### Task 2: Create HangarScreen implementation

This is the big task. The implementation is a direct migration of `hangar.cpp` — same logic, but static locals become `this->` member access, and navigation uses UIManager.

**Files:**
- Create: `neuroflyer/src/ui/screens/hangar/hangar_screen.cpp`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Create the implementation**

Create `neuroflyer/src/ui/screens/hangar/hangar_screen.cpp`. This file should be a direct port of the existing `hangar.cpp` with these changes:

1. All references to `s_genomes` → `genomes_`, `s_selected_genome_idx` → `selected_genome_idx_`, `s_preview` → `preview_`, `s_sub_view` → `sub_view_`, etc.
2. `draw_genome_list()` becomes a method: `HangarScreen::Action HangarScreen::draw_genome_list(AppState& state, Renderer& renderer, UIManager& ui)`
3. The refresh logic moves to `HangarScreen::refresh_genomes(AppState& state)`
4. Navigation: "Back to Menu" calls `ui.pop_screen()` instead of `go_to_screen(state, Screen::MainMenu)`
5. Navigation: "Create Genome" and "Select Genome" still use `go_to_screen()` for now (those screens are still legacy)
6. Escape from GenomeList calls `ui.pop_screen()`
7. `on_enter()` sets `state.genomes_dirty = true` (not accessible — actually, just set a local dirty flag or refresh in on_draw when needed)

**IMPORTANT:** The implementer MUST read the existing `neuroflyer/src/ui/screens/hangar/hangar.cpp` file completely and port ALL its logic. Do not skip any functionality. The new file is a member-based rewrite of the same code.

- [ ] **Step 2: Add to CMakeLists.txt**

Add after the existing `src/ui/screens/hangar/hangar.cpp` line:

```cmake
    src/ui/screens/hangar/hangar_screen.cpp
```

- [ ] **Step 3: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`

- [ ] **Step 4: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/src/ui/screens/hangar/hangar_screen.cpp neuroflyer/CMakeLists.txt && \
git commit -m "feat(neuroflyer): HangarScreen implementation — migrated from static draw_hangar"
```

---

### Task 3: Wire HangarScreen into MainMenuScreen

**Files:**
- Modify: `neuroflyer/src/ui/screens/menu/main_menu_screen.cpp`

- [ ] **Step 1: Replace LegacyScreen wrapper with real HangarScreen**

In `main_menu_screen.cpp`, add the include:

```cpp
#include <neuroflyer/ui/screens/hangar_screen.h>
```

Replace the Hangar button handler from:

```cpp
    if (ImGui::Button("Hangar", ImVec2(BTN_W, BTN_H))) {
        ui.push_screen(std::make_unique<LegacyScreen>("Hangar",
            [](AppState& s, Renderer& r) { draw_hangar(s, r); }));
    }
```

To:

```cpp
    if (ImGui::Button("Hangar", ImVec2(BTN_W, BTN_H))) {
        ui.push_screen(std::make_unique<HangarScreen>());
    }
```

Remove the `#include <neuroflyer/screens/hangar/hangar.h>` if no longer needed.

- [ ] **Step 2: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`

- [ ] **Step 3: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/src/ui/screens/menu/main_menu_screen.cpp && \
git commit -m "feat(neuroflyer): MainMenuScreen pushes HangarScreen directly"
```

---

### Task 4: Smoke test

- [ ] **Step 1: Build and launch**

- [ ] **Step 2: Test hangar navigation**
1. Main menu → Hangar → genome list appears
2. Hover genomes → preview topology renders in right panel
3. Double-click genome → variant viewer opens
4. Escape back → hangar state preserved (same genome selected)
5. Escape again → main menu

- [ ] **Step 3: Test sub-views**
1. Select genome → open Net Viewer → displays network → close returns to list
2. Select genome → open Test Bench → sensor view works → cancel returns to list
3. Fitness editor opens and closes

- [ ] **Step 4: Test genome CRUD**
1. Create new genome → returns to hangar with new genome in list
2. Delete genome → confirmation → genome removed

- [ ] **Step 5: Commit any fixes**
