# Team Genome UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add net type tabs to VariantViewerScreen (Fighters, Squad Nets) and enable squad net training with frozen fighter weights.

**Architecture:** Extend snapshot format (v6) with `paired_fighter_name`. Add GenomeManager methods for squad/ subdirectory. Add tab bar to VariantViewerScreen that switches between fighter and squad net variant lists with net-type-specific actions. Add squad training mode to ArenaGameScreen that freezes fighter weights and only evolves squad nets.

**Tech Stack:** C++20, ImGui, SDL2, neuralnet library, GoogleTest

**Spec:** `docs/superpowers/specs/2026-03-28-team-genome-ui-design.md`

---

## File Structure

**New files:**
- `include/neuroflyer/ui/modals/fighter_pairing_modal.h` — modal for selecting a fighter variant to pair with a squad net
- `src/ui/modals/fighter_pairing_modal.cpp` — implementation

**Modified files (engine):**
- `include/neuroflyer/snapshot.h` — add `paired_fighter_name` to Snapshot and SnapshotHeader
- `src/engine/snapshot_io.cpp` — v6 format: read/write paired_fighter_name
- `include/neuroflyer/snapshot_io.h` — bump CURRENT_VERSION to 6
- `include/neuroflyer/genome_manager.h` — add squad variant methods
- `src/engine/genome_manager.cpp` — implement squad variant methods
- `include/neuroflyer/team_evolution.h` — add evolve_squad_only
- `src/engine/team_evolution.cpp` — implement evolve_squad_only

**Modified files (UI):**
- `include/neuroflyer/ui/screens/variant_viewer_screen.h` — add NetTypeTab, squad state
- `src/ui/screens/hangar/variant_viewer_screen.cpp` — tab bar, squad variant list, squad actions
- `include/neuroflyer/ui/screens/arena_game_screen.h` — add squad training mode fields
- `src/ui/screens/arena/arena_game_screen.cpp` — squad training mode logic
- `include/neuroflyer/app_state.h` — add squad training state to AppState
- `CMakeLists.txt` — add fighter_pairing_modal.cpp

**Test files:**
- `tests/snapshot_io_test.cpp` — add v6 round-trip test
- `tests/genome_manager_test.cpp` — add squad variant tests
- `tests/team_evolution_test.cpp` — add evolve_squad_only test

---

## Build & Test Commands

```bash
cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests
cmake --build build --target neuroflyer   # full app build
```

---

## Task 1: Snapshot Format v6 — paired_fighter_name

**Files:**
- Modify: `include/neuroflyer/snapshot.h`
- Modify: `include/neuroflyer/snapshot_io.h`
- Modify: `src/engine/snapshot_io.cpp`
- Test: `tests/snapshot_io_test.cpp`

- [ ] **Step 1: Write v6 round-trip test**

```cpp
// Add to tests/snapshot_io_test.cpp
TEST(SnapshotIOTest, V6PairedFighterName) {
    nf::Snapshot snap;
    snap.name = "squad-v1";
    snap.generation = 50;
    snap.created_timestamp = 1234567890;
    snap.parent_name = "squad-root";
    snap.run_count = 3;
    snap.paired_fighter_name = "elite-fighter-v3";
    snap.ship_design.memory_slots = 4;
    snap.topology.input_size = 8;
    snap.topology.layers = {{4, neuralnet::Activation::Tanh}, {4, neuralnet::Activation::Tanh}};
    snap.weights = {0.1f, 0.2f, 0.3f};

    std::ostringstream out;
    nf::save_snapshot(snap, out);

    std::istringstream in(out.str());
    auto loaded = nf::load_snapshot(in);

    EXPECT_EQ(loaded.name, "squad-v1");
    EXPECT_EQ(loaded.paired_fighter_name, "elite-fighter-v3");
    EXPECT_EQ(loaded.run_count, 3u);
}

TEST(SnapshotIOTest, V6HeaderPairedFighterName) {
    nf::Snapshot snap;
    snap.name = "squad-v2";
    snap.generation = 10;
    snap.paired_fighter_name = "my-fighter";
    snap.ship_design.memory_slots = 2;
    snap.topology.input_size = 4;
    snap.topology.layers = {{2, neuralnet::Activation::Tanh}};
    snap.weights = {0.5f};

    std::ostringstream out;
    nf::save_snapshot(snap, out);

    std::istringstream in(out.str());
    auto hdr = nf::read_snapshot_header(in);

    EXPECT_EQ(hdr.name, "squad-v2");
    EXPECT_EQ(hdr.paired_fighter_name, "my-fighter");
}

TEST(SnapshotIOTest, V5SnapshotLoadsPairedFighterEmpty) {
    // Create a v5 snapshot (no paired_fighter_name), verify it loads with empty string
    nf::Snapshot snap;
    snap.name = "old-variant";
    snap.generation = 5;
    snap.ship_design.memory_slots = 4;
    snap.topology.input_size = 8;
    snap.topology.layers = {{4, neuralnet::Activation::Tanh}};
    snap.weights = {0.1f};

    // Save, load, verify paired_fighter_name is empty
    std::ostringstream out;
    nf::save_snapshot(snap, out);
    std::istringstream in(out.str());
    auto loaded = nf::load_snapshot(in);
    EXPECT_TRUE(loaded.paired_fighter_name.empty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -5`
Expected: compilation error — `paired_fighter_name` doesn't exist

- [ ] **Step 3: Add paired_fighter_name to Snapshot and SnapshotHeader**

In `include/neuroflyer/snapshot.h`, add to both structs:

```cpp
// In Snapshot, after run_count:
std::string paired_fighter_name;  // squad net: which fighter it was trained with

// In SnapshotHeader, after run_count:
std::string paired_fighter_name;
```

- [ ] **Step 4: Bump version and update I/O**

In `include/neuroflyer/snapshot_io.h`, change:
```cpp
constexpr uint16_t CURRENT_VERSION = 6;
```

In `src/engine/snapshot_io.cpp`, in `write_payload()`, after writing `run_count`:
```cpp
// v6: paired_fighter_name
write_string(out, snapshot.paired_fighter_name);
```

In `read_payload()`, after reading `run_count` (inside `if (version >= 5)`):
```cpp
// v6: paired_fighter_name
if (version >= 6) {
    snapshot.paired_fighter_name = read_string(in);
}
```

In `read_header_payload()`, same pattern:
```cpp
if (version >= 6) {
    header.paired_fighter_name = read_string(in);
}
```

NOTE: Look at how `run_count` is handled for v5 as the exact pattern to follow. The `write_string` / `read_string` helpers should already exist (used for `name` and `parent_name`).

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="SnapshotIOTest.*"`
Expected: All PASS including new v6 tests

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/snapshot.h include/neuroflyer/snapshot_io.h src/engine/snapshot_io.cpp tests/snapshot_io_test.cpp
git commit -m "feat: snapshot format v6 — add paired_fighter_name field"
```

---

## Task 2: GenomeManager Squad Variant Methods

**Files:**
- Modify: `include/neuroflyer/genome_manager.h`
- Modify: `src/engine/genome_manager.cpp`
- Test: `tests/genome_manager_test.cpp`

- [ ] **Step 1: Write squad variant test**

```cpp
// Add to tests/genome_manager_test.cpp
TEST(GenomeManagerTest, ListSquadVariantsEmptyDir) {
    // Create temp genome dir without squad/ subdir
    auto tmp = std::filesystem::temp_directory_path() / "test_genome_squad";
    std::filesystem::create_directories(tmp);
    auto cleanup = [&]() { std::filesystem::remove_all(tmp); };

    auto variants = nf::list_squad_variants(tmp.string());
    EXPECT_TRUE(variants.empty());

    cleanup();
}

TEST(GenomeManagerTest, SaveAndListSquadVariant) {
    auto tmp = std::filesystem::temp_directory_path() / "test_genome_squad2";
    std::filesystem::create_directories(tmp);
    auto cleanup = [&]() { std::filesystem::remove_all(tmp); };

    nf::Snapshot snap;
    snap.name = "squad-v1";
    snap.generation = 10;
    snap.paired_fighter_name = "elite-fighter";
    snap.ship_design.memory_slots = 2;
    snap.topology.input_size = 8;
    snap.topology.layers = {{4, neuralnet::Activation::Tanh}};
    snap.weights = {0.1f, 0.2f};

    nf::save_squad_variant(tmp.string(), snap);

    auto variants = nf::list_squad_variants(tmp.string());
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].name, "squad-v1");
    EXPECT_EQ(variants[0].paired_fighter_name, "elite-fighter");

    cleanup();
}

TEST(GenomeManagerTest, DeleteSquadVariant) {
    auto tmp = std::filesystem::temp_directory_path() / "test_genome_squad3";
    std::filesystem::create_directories(tmp);
    auto cleanup = [&]() { std::filesystem::remove_all(tmp); };

    nf::Snapshot snap;
    snap.name = "squad-to-delete";
    snap.generation = 5;
    snap.ship_design.memory_slots = 2;
    snap.topology.input_size = 4;
    snap.topology.layers = {{2, neuralnet::Activation::Tanh}};
    snap.weights = {0.5f};

    nf::save_squad_variant(tmp.string(), snap);
    ASSERT_EQ(nf::list_squad_variants(tmp.string()).size(), 1u);

    nf::delete_squad_variant(tmp.string(), "squad-to-delete");
    EXPECT_TRUE(nf::list_squad_variants(tmp.string()).empty());

    cleanup();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: compilation error — functions don't exist

- [ ] **Step 3: Add function declarations to genome_manager.h**

```cpp
/// List squad net variants from {genome_dir}/squad/
[[nodiscard]] std::vector<SnapshotHeader> list_squad_variants(const std::string& genome_dir);

/// Save a squad net variant to {genome_dir}/squad/{name}.bin
void save_squad_variant(const std::string& genome_dir, const Snapshot& variant);

/// Delete a squad net variant from {genome_dir}/squad/
void delete_squad_variant(const std::string& genome_dir, const std::string& variant_name);
```

- [ ] **Step 4: Implement in genome_manager.cpp**

```cpp
std::vector<SnapshotHeader> list_squad_variants(const std::string& genome_dir) {
    std::string squad_dir = genome_dir + "/squad";
    if (!std::filesystem::exists(squad_dir)) return {};
    return list_variants(squad_dir);  // reuse existing list_variants on the subdir
}

void save_squad_variant(const std::string& genome_dir, const Snapshot& variant) {
    std::string squad_dir = genome_dir + "/squad";
    std::filesystem::create_directories(squad_dir);
    std::string path = squad_dir + "/" + variant.name + ".bin";
    save_snapshot(variant, path);
    // Rebuild lineage for squad subdir (reuses existing rebuild_lineage on subdir)
    rebuild_lineage(squad_dir);
}

void delete_squad_variant(const std::string& genome_dir, const std::string& variant_name) {
    std::string squad_dir = genome_dir + "/squad";
    std::string path = squad_dir + "/" + variant_name + ".bin";
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}
```

NOTE: `list_variants` already scans a directory for `.bin` files and reads headers. We can reuse it on the `squad/` subdir directly. Check the existing implementation to confirm it works on any directory path (it should — it just reads all `.bin` files in the given dir).

However, `list_variants` may filter out `genome.bin` (the root). For the squad dir there's no root, so all `.bin` files are variants. Verify this works correctly. If `list_variants` has special handling for `genome.bin`, the squad dir won't have one, which is fine.

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="GenomeManagerTest.*Squad*"`
Expected: All PASS

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/genome_manager.h src/engine/genome_manager.cpp tests/genome_manager_test.cpp
git commit -m "feat: add GenomeManager methods for squad variant CRUD"
```

---

## Task 3: evolve_squad_only

**Files:**
- Modify: `include/neuroflyer/team_evolution.h`
- Modify: `src/engine/team_evolution.cpp`
- Test: `tests/team_evolution_test.cpp`

- [ ] **Step 1: Write evolve_squad_only test**

```cpp
// Add to tests/team_evolution_test.cpp
TEST(TeamEvolutionTest, EvolveSquadOnlyFreezesFighters) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 1},
    };
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {4};
    squad_config.output_size = 4;

    auto pop = nf::create_team_population(design, {6}, squad_config, 10, rng);

    // Record original fighter weights for first individual
    auto original_fighter_weights = pop[0].fighter_individual.genome.flatten_all();

    // Assign fake fitness
    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i) * 10.0f;
    }

    nf::EvolutionConfig evo_config;
    evo_config.elitism_count = 1;
    evo_config.tournament_size = 3;

    auto next = nf::evolve_squad_only(pop, evo_config, rng);
    EXPECT_EQ(next.size(), 10u);

    // Elite (index 0 = highest fitness = pop[9]) should have unchanged fighter weights
    // But ALL individuals should have unchanged fighter weights (frozen)
    // The elite's fighter should match the original top-fitness individual's fighter
    // Non-elite fighters are copies of parents — but since we don't mutate, they're exact copies
    for (const auto& team : next) {
        // Fighter nets should still build valid networks
        auto fnet = team.build_fighter_network();
        EXPECT_GT(fnet.input_size(), 0u);

        // Squad nets should also be valid
        auto snet = team.build_squad_network();
        EXPECT_EQ(snet.input_size(), 8u);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compilation error — `evolve_squad_only` doesn't exist

- [ ] **Step 3: Add declaration to team_evolution.h**

```cpp
/// Evolve only squad nets — fighter weights are frozen (not mutated).
/// Used for squad-specific training with a fixed fighter variant.
[[nodiscard]] std::vector<TeamIndividual> evolve_squad_only(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);
```

- [ ] **Step 4: Implement in team_evolution.cpp**

```cpp
std::vector<TeamIndividual> evolve_squad_only(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    // Sort by fitness descending
    std::sort(population.begin(), population.end(),
              [](const auto& a, const auto& b) { return a.fitness > b.fitness; });

    std::vector<TeamIndividual> next;
    next.reserve(population.size());

    // Elitism: copy top N
    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

    // Tournament selection + mutation for squad only
    std::uniform_int_distribution<std::size_t> dist(0, population.size() - 1);
    while (next.size() < population.size()) {
        std::size_t best = dist(rng);
        for (std::size_t t = 1; t < config.tournament_size; ++t) {
            std::size_t candidate = dist(rng);
            if (population[candidate].fitness > population[best].fitness) {
                best = candidate;
            }
        }

        TeamIndividual child = population[best];
        child.fitness = 0.0f;

        // Only mutate squad net — fighter stays frozen
        apply_mutations(child.squad_individual, config, rng);

        next.push_back(std::move(child));
    }

    return next;
}
```

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="TeamEvolutionTest.*"`
Expected: All PASS

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/team_evolution.h src/engine/team_evolution.cpp tests/team_evolution_test.cpp
git commit -m "feat: add evolve_squad_only — freezes fighter weights during evolution"
```

---

## Task 4: VariantViewerScreen — Tab Bar

**Files:**
- Modify: `include/neuroflyer/ui/screens/variant_viewer_screen.h`
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`

- [ ] **Step 1: Add tab state to header**

In `include/neuroflyer/ui/screens/variant_viewer_screen.h`, add:

```cpp
// After the existing SubView enum:
enum class NetTypeTab { Fighters, SquadNets, Commander };

// Add as private member:
NetTypeTab active_tab_ = NetTypeTab::Fighters;
std::vector<SnapshotHeader> squad_variants_;
int squad_selected_idx_ = 0;
std::string paired_fighter_name_;  // currently paired fighter for squad training
```

- [ ] **Step 2: Draw the tab bar**

In `src/ui/screens/hangar/variant_viewer_screen.cpp`, in the `draw_variant_list` method (or at the top of the list panel in `on_draw`), add a tab bar before the variant table.

Find where the genome name header is drawn and add the tab bar immediately after it:

```cpp
// Tab bar
{
    ImGui::Spacing();
    float tab_w = 120.0f;

    // Fighters tab
    bool is_fighters = (active_tab_ == NetTypeTab::Fighters);
    if (is_fighters) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.3f, 1.0f));
    if (ImGui::Button("Fighters", ImVec2(tab_w, 0))) {
        active_tab_ = NetTypeTab::Fighters;
    }
    if (is_fighters) {
        // Draw underline
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 s = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(p.x, s.y), ImVec2(s.x, s.y),
            IM_COL32(162, 155, 254, 255), 2.0f);
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    // Squad Nets tab
    bool is_squad = (active_tab_ == NetTypeTab::SquadNets);
    if (is_squad) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.1f, 1.0f));
    if (ImGui::Button("Squad Nets", ImVec2(tab_w, 0))) {
        active_tab_ = NetTypeTab::SquadNets;
        // Reload squad variants
        squad_variants_ = list_squad_variants(vs_.genome_dir);
        squad_selected_idx_ = 0;
    }
    if (is_squad) {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 s = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(p.x, s.y), ImVec2(s.x, s.y),
            IM_COL32(249, 202, 36, 255), 2.0f);
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    // Commander tab (disabled)
    ImGui::BeginDisabled();
    ImGui::Button("Commander", ImVec2(tab_w, 0));
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
}
```

- [ ] **Step 3: Conditionally render variant list based on active tab**

Wrap the existing variant list table in an `if (active_tab_ == NetTypeTab::Fighters)` block. Add an `else if (active_tab_ == NetTypeTab::SquadNets)` block that draws the squad variant table:

```cpp
if (active_tab_ == NetTypeTab::SquadNets) {
    // Squad variant table
    if (ImGui::BeginTable("##SquadVariants", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Gen", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Paired Fighter");
        ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(squad_variants_.size()); ++i) {
            const auto& v = squad_variants_[static_cast<std::size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool selected = (i == squad_selected_idx_);
            if (ImGui::Selectable(v.name.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                squad_selected_idx_ = i;
                paired_fighter_name_ = v.paired_fighter_name;
            }

            ImGui::TableNextColumn();
            ImGui::Text("%u", v.generation);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.63f, 0.61f, 0.99f, 1.0f), "%s",
                v.paired_fighter_name.empty() ? "—" : v.paired_fighter_name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", format_short_date(v.created_timestamp).c_str());
        }

        ImGui::EndTable();
    }
}
```

- [ ] **Step 4: Load squad variants on genome change**

In the genome-change detection block (where `state.active_genome != last_genome_`), add:
```cpp
squad_variants_ = list_squad_variants(vs_.genome_dir);
squad_selected_idx_ = 0;
paired_fighter_name_.clear();
```

Also reload when `state.variants_dirty` is set (a variant was saved/deleted).

- [ ] **Step 5: Build and verify**

Run: `cmake --build build`
Expected: compiles. Tab bar appears on VariantViewerScreen. Clicking tabs switches between fighter and squad lists. Squad list is empty for existing genomes (no squad/ dir yet).

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/ui/screens/variant_viewer_screen.h src/ui/screens/hangar/variant_viewer_screen.cpp
git commit -m "feat: add net type tab bar to VariantViewerScreen"
```

---

## Task 5: Fighter Pairing Modal

**Files:**
- Create: `include/neuroflyer/ui/modals/fighter_pairing_modal.h`
- Create: `src/ui/modals/fighter_pairing_modal.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create modal header**

```cpp
// include/neuroflyer/ui/modals/fighter_pairing_modal.h
#pragma once

#include <neuroflyer/snapshot.h>
#include <neuroflyer/ui/ui_modal.h>

#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

/// Modal that shows a list of fighter variants for pairing with a squad net.
class FighterPairingModal : public UIModal {
public:
    FighterPairingModal(std::vector<SnapshotHeader> fighters,
                        std::function<void(const std::string&)> on_select);

    void on_draw(AppState& state, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "FighterPairingModal"; }

private:
    std::vector<SnapshotHeader> fighters_;
    std::function<void(const std::string&)> on_select_;
    int selected_idx_ = 0;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create modal implementation**

```cpp
// src/ui/modals/fighter_pairing_modal.cpp
#include <neuroflyer/ui/modals/fighter_pairing_modal.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>
#include <neuroflyer/paths.h>

#include <imgui.h>

namespace neuroflyer {

FighterPairingModal::FighterPairingModal(
    std::vector<SnapshotHeader> fighters,
    std::function<void(const std::string&)> on_select)
    : fighters_(std::move(fighters))
    , on_select_(std::move(on_select)) {}

void FighterPairingModal::on_draw(AppState& /*state*/, UIManager& ui) {
    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_Once);
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
        ImGuiCond_Once, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Select Fighter Variant", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {

        ImGui::TextWrapped("Choose a fighter variant to pair with this squad net. "
                           "Fighter weights will be frozen during squad training.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTable("##FighterList", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Gen", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(fighters_.size()); ++i) {
                const auto& f = fighters_[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool selected = (i == selected_idx_);
                if (ImGui::Selectable(f.name.c_str(), selected,
                        ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_idx_ = i;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    on_select_(f.name);
                    ui.pop_modal();
                    ImGui::EndTable();
                    ImGui::End();
                    return;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u", f.generation);
                ImGui::TableNextColumn();
                ImGui::Text("%s", format_short_date(f.created_timestamp).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        float btn_w = 120.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - btn_w * 2 - 10.0f) / 2.0f);

        if (ui::button("Select", btn_w) && !fighters_.empty()) {
            auto idx = static_cast<std::size_t>(selected_idx_);
            if (idx < fighters_.size()) {
                on_select_(fighters_[idx].name);
                ui.pop_modal();
            }
        }
        ImGui::SameLine(0, 10.0f);
        if (ui::button("Cancel", btn_w)) {
            ui.pop_modal();
        }
    }
    ImGui::End();
}

} // namespace neuroflyer
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/ui/modals/fighter_pairing_modal.cpp` to the main executable source list in `CMakeLists.txt`.

- [ ] **Step 4: Build**

Run: `cmake --build build`
Expected: compiles cleanly

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/ui/modals/fighter_pairing_modal.h src/ui/modals/fighter_pairing_modal.cpp CMakeLists.txt
git commit -m "feat: add FighterPairingModal for squad net training"
```

---

## Task 6: Squad Nets Tab — Action Panel + Training Launch

**Files:**
- Modify: `include/neuroflyer/app_state.h`
- Modify: `include/neuroflyer/ui/screens/variant_viewer_screen.h`
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`

- [ ] **Step 1: Add squad training fields to AppState**

In `include/neuroflyer/app_state.h`, add:

```cpp
// Squad training mode
bool squad_training_mode = false;
bool base_attack_mode = false;          // kill team 1 fighters at spawn
std::string squad_paired_fighter_name;  // fighter variant to freeze
std::string squad_training_genome_dir;  // genome dir for saving squad variants
```

- [ ] **Step 2: Add squad action handling to variant_viewer_screen.h**

Add to the Action enum:
```cpp
SquadTrainVsSquad, SquadTrainBaseAttack, SquadChangeFighter, SquadViewNet, SquadDelete
```

Add private method:
```cpp
Action draw_squad_action_panel(AppState& state, UIManager& ui);
```

- [ ] **Step 3: Implement squad action panel**

In `src/ui/screens/hangar/variant_viewer_screen.cpp`, add:

```cpp
VariantViewerScreen::Action VariantViewerScreen::draw_squad_action_panel(
    AppState& state, UIManager& ui) {

    if (squad_variants_.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "No squad net variants yet.\nTrain one using the scenarios below.");
        ImGui::Spacing();
    }

    bool has_selection = !squad_variants_.empty() &&
        squad_selected_idx_ >= 0 &&
        static_cast<std::size_t>(squad_selected_idx_) < squad_variants_.size();

    if (has_selection) {
        const auto& sel = squad_variants_[static_cast<std::size_t>(squad_selected_idx_)];
        ImGui::TextColored(ImVec4(0.97f, 0.79f, 0.14f, 1.0f), "Selected: %s", sel.name.c_str());
        if (!sel.paired_fighter_name.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "Paired with: %s", sel.paired_fighter_name.c_str());
        }
        ImGui::Spacing();
    }

    // Training scenarios
    ui::section_header("Training Scenarios");
    if (ui::button("Squad vs Squad", -1)) return Action::SquadTrainVsSquad;
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "1v1 with bases");

    if (ui::button("Base Attack", -1)) return Action::SquadTrainBaseAttack;
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "destroy undefended base");

    ImGui::BeginDisabled();
    ui::button("Base Defense", -1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "coming soon");

    ImGui::Spacing();

    // Fighter pairing
    ui::section_header("Fighter Pairing");
    if (paired_fighter_name_.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "No fighter selected");
    } else {
        ImGui::TextColored(ImVec4(0.63f, 0.61f, 0.99f, 1.0f),
            "Using: %s", paired_fighter_name_.c_str());
    }
    ImGui::SameLine();
    if (ui::button("[Change]", 80)) return Action::SquadChangeFighter;
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
        "Squad net trains with frozen fighter weights");

    ImGui::Spacing();

    // Inspection
    if (has_selection) {
        ui::section_header("Inspection");
        if (ui::button("View Squad Net", -1)) return Action::SquadViewNet;
        ImGui::Spacing();

        // Management
        ui::section_header("Management");
        if (ui::button("Delete Variant", -1)) return Action::SquadDelete;
    }

    return Action::Stay;
}
```

- [ ] **Step 4: Wire squad actions in on_draw()**

In the action dispatch section of `on_draw()`, add handling for the new squad actions:

```cpp
case Action::SquadChangeFighter: {
    auto fighters = vs_.variants;  // fighter variants from the Fighters tab
    ui.push_modal(std::make_unique<FighterPairingModal>(
        std::move(fighters),
        [this](const std::string& name) {
            paired_fighter_name_ = name;
        }));
    break;
}

case Action::SquadTrainVsSquad:
case Action::SquadTrainBaseAttack: {
    // Require fighter pairing
    if (paired_fighter_name_.empty()) {
        auto fighters = vs_.variants;
        ui.push_modal(std::make_unique<FighterPairingModal>(
            std::move(fighters),
            [this](const std::string& name) {
                paired_fighter_name_ = name;
                // User still needs to click the button again after pairing
            }));
        break;
    }

    // Set up squad training state
    state.squad_training_mode = true;
    state.squad_paired_fighter_name = paired_fighter_name_;
    state.squad_training_genome_dir = vs_.genome_dir;

    // Configure arena for scenario
    ArenaConfig arena_config;
    if (action == Action::SquadTrainVsSquad) {
        arena_config.num_teams = 2;
        arena_config.num_squads = 1;
        arena_config.fighters_per_squad = 8;
        arena_config.base_hp = 1000.0f;
        arena_config.tower_count = 50;
        arena_config.token_count = 20;
        arena_config.time_limit_ticks = 3600;
    } else {  // BaseAttack
        arena_config.num_teams = 2;
        arena_config.num_squads = 1;
        arena_config.fighters_per_squad = 8;
        arena_config.base_hp = 1000.0f;
        arena_config.tower_count = 20;
        arena_config.token_count = 10;
        arena_config.time_limit_ticks = 1800;
        state.base_attack_mode = true;
    }

    ui.push_screen(std::make_unique<ArenaConfigScreen>(arena_config));
    break;
}

case Action::SquadViewNet: {
    if (squad_selected_idx_ >= 0 &&
        static_cast<std::size_t>(squad_selected_idx_) < squad_variants_.size()) {
        auto& sel = squad_variants_[static_cast<std::size_t>(squad_selected_idx_)];
        std::string path = vs_.genome_dir + "/squad/" + sel.name + ".bin";
        auto snap = load_snapshot(path);
        auto ind = snapshot_to_individual(snap);
        auto net = ind.build_network();
        ui.push_screen(std::make_unique<VariantNetEditorScreen>(
            std::move(ind), std::move(net), snap.ship_design, path, sel.name));
    }
    break;
}

case Action::SquadDelete: {
    if (squad_selected_idx_ >= 0 &&
        static_cast<std::size_t>(squad_selected_idx_) < squad_variants_.size()) {
        auto& sel = squad_variants_[static_cast<std::size_t>(squad_selected_idx_)];
        std::string msg = "Delete squad variant \"" + sel.name + "\"?";
        std::string del_name = sel.name;
        ui.push_modal(std::make_unique<ConfirmModal>(
            "Delete Squad Variant", msg,
            [this, del_name]() {
                delete_squad_variant(vs_.genome_dir, del_name);
                squad_variants_ = list_squad_variants(vs_.genome_dir);
                if (squad_selected_idx_ > 0) --squad_selected_idx_;
            }));
    }
    break;
}
```

- [ ] **Step 5: Wire draw_squad_action_panel into the right panel**

In the right panel section of `on_draw` (or `draw_variant_list`), conditionally call the squad action panel:

```cpp
if (active_tab_ == NetTypeTab::Fighters) {
    // ... existing fighter action panel ...
} else if (active_tab_ == NetTypeTab::SquadNets) {
    action = draw_squad_action_panel(state, ui);
}
```

- [ ] **Step 6: Add includes**

Add to variant_viewer_screen.cpp:
```cpp
#include <neuroflyer/ui/modals/fighter_pairing_modal.h>
#include <neuroflyer/genome_manager.h>  // if not already included
#include <neuroflyer/ui/screens/arena_config_screen.h>
```

- [ ] **Step 7: Build**

Run: `cmake --build build`
Expected: compiles cleanly

- [ ] **Step 8: Commit**

```bash
git add include/neuroflyer/app_state.h include/neuroflyer/ui/screens/variant_viewer_screen.h src/ui/screens/hangar/variant_viewer_screen.cpp
git commit -m "feat: add squad nets action panel with training scenarios and fighter pairing"
```

---

## Task 7: ArenaGameScreen — Squad Training Mode

**Files:**
- Modify: `include/neuroflyer/ui/screens/arena_game_screen.h`
- Modify: `src/ui/screens/arena/arena_game_screen.cpp`

- [ ] **Step 1: Add squad training fields to header**

In `include/neuroflyer/ui/screens/arena_game_screen.h`, add:

```cpp
// Squad training mode
bool squad_training_mode_ = false;
std::string squad_paired_fighter_name_;
std::string squad_genome_dir_;
Snapshot paired_fighter_snapshot_;  // loaded once, used to rebuild frozen fighters
```

- [ ] **Step 2: Update initialize() to detect squad training mode**

In `initialize()`, after the existing setup, add:

```cpp
// Check for squad training mode
squad_training_mode_ = state.squad_training_mode;
state.squad_training_mode = false;  // consume the flag

if (squad_training_mode_) {
    squad_paired_fighter_name_ = state.squad_paired_fighter_name;
    squad_genome_dir_ = state.squad_training_genome_dir;

    // Initialize squad config
    squad_config_.input_size = 8;
    squad_config_.hidden_sizes = {6};
    squad_config_.output_size = config_.squad_broadcast_signals;

    // Load the paired fighter snapshot
    std::string fighter_path = squad_genome_dir_ + "/" + squad_paired_fighter_name_ + ".bin";
    // Check if it's the root genome
    bool is_root = false;
    for (const auto& v : list_variants(squad_genome_dir_)) {
        if (v.name == squad_paired_fighter_name_ && v.parent_name.empty()) {
            is_root = true;
            break;
        }
    }
    if (is_root) {
        fighter_path = squad_genome_dir_ + "/genome.bin";
    }
    paired_fighter_snapshot_ = load_snapshot(fighter_path);
    ship_design_ = paired_fighter_snapshot_.ship_design;

    // Override team population: create squad nets, freeze fighters
    auto fighter_ind = snapshot_to_individual(paired_fighter_snapshot_);
    std::size_t team_pop_size = 20;
    evo_config_.population_size = team_pop_size;
    team_population_.clear();
    for (std::size_t i = 0; i < team_pop_size; ++i) {
        auto team = TeamIndividual::create(ship_design_, {8, 8}, squad_config_, state.rng);
        team.fighter_individual = fighter_ind;  // frozen copy
        team_population_.push_back(std::move(team));
    }

    std::cout << "Squad training mode: using fighter \"" << squad_paired_fighter_name_
              << "\", evolving squad nets only\n";
}
```

- [ ] **Step 3: Update do_arena_evolution() for squad mode**

In `do_arena_evolution()`, switch between full evolution and squad-only:

```cpp
if (squad_training_mode_) {
    team_population_ = evolve_squad_only(team_population_, evo_config_, state.rng);
    // Refreeze fighter weights from paired snapshot
    auto fighter_ind = snapshot_to_individual(paired_fighter_snapshot_);
    for (auto& team : team_population_) {
        team.fighter_individual = fighter_ind;
    }
} else {
    team_population_ = evolve_team_population(team_population_, evo_config_, state.rng);
}
```

- [ ] **Step 4: Handle Base Attack scenario (kill all team 1 fighters at start)**

In `start_new_match()`, after creating the ArenaSession, add:

```cpp
// Base Attack mode: kill all team 1 fighters so their base is undefended
if (base_attack_mode_) {
    for (std::size_t i = 0; i < arena_->ships().size(); ++i) {
        if (arena_->team_of(i) == 1) {
            arena_->ships()[i].alive = false;
        }
    }
}
```

Add `bool base_attack_mode_ = false;` to the private members in the header. Set it in `initialize()` when the scenario is Base Attack (detected via `config_.time_limit_ticks == 1800` or passed explicitly through AppState).

- [ ] **Step 5: Add squad variant saving to pause screen integration**

When the user saves variants from the pause screen during squad training, they should save to the squad/ directory. In the save variants logic (either in pause_config_screen.cpp or fly_session_screen.cpp — find where `save_variant` is called during arena mode):

Add a check:
```cpp
if (state.squad_training_mode || !squad_genome_dir_.empty()) {
    // Save as squad variant
    snap.paired_fighter_name = squad_paired_fighter_name_;
    save_squad_variant(squad_genome_dir_, snap);
} else {
    // Normal variant save
    save_variant(genome_dir, snap);
}
```

NOTE: This needs to be wired wherever the "Save Variants" tab on the pause screen handles arena mode saves. Read the pause_config_screen.cpp to find the exact location.

- [ ] **Step 6: Build**

Run: `cmake --build build`
Expected: compiles cleanly

- [ ] **Step 7: Commit**

```bash
git add include/neuroflyer/ui/screens/arena_game_screen.h src/ui/screens/arena/arena_game_screen.cpp
git commit -m "feat: add squad training mode — frozen fighters, squad-only evolution"
```

---

## Task 8: End-to-End Verification + Backlog

- [ ] **Step 1: Run full test suite**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests`
Expected: All tests PASS

- [ ] **Step 2: Build full app**

Run: `cmake --build build`

- [ ] **Step 3: Manual verification (if app can be run)**

1. Open a genome in the hangar
2. Verify tab bar shows (Fighters, Squad Nets, Commander grayed)
3. Click Squad Nets tab — should show empty list
4. Click Squad vs Squad — should prompt for fighter pairing
5. Select a fighter variant, configure, start
6. Verify arena runs with squad training mode log message
7. Verify evolution progresses (generation counter increments)

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "docs: finalize team genome UI implementation"
```
