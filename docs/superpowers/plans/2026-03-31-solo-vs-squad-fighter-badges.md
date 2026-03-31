# Solo vs Squad Fighter Badges & Filter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `[SOLO]`/`[SQUAD]` type badges and an All/Solo/Squad filter toggle to the Fighters tab in the Variant Viewer so users can distinguish original scroller variants from squad-converted fighters at a glance.

**Architecture:** Read `SnapshotHeader::net_type` (already loaded) in the Fighters tab row loop to render a colored badge before each variant name. A `FilterMode` enum member on `VariantViewerScreen` drives a filter button row above the table; the row loop builds a filtered index list and maps selection correctly. No storage changes — `NetType` is already stored in every snapshot header.

**Tech Stack:** C++20, ImGui, existing `ui::button()` widget, `NetType` enum from `snapshot.h`.

---

## File Map

| File | Change |
|------|--------|
| `include/neuroflyer/ui/screens/variant_viewer_screen.h` | Add `FilterMode` enum; add `fighter_filter_` member |
| `src/ui/screens/hangar/variant_viewer_screen.cpp` | Add filter buttons, badge rendering, filtered row loop, genome-switch reset |

---

### Task 1: Add `FilterMode` enum and member to the header

**Files:**
- Modify: `include/neuroflyer/ui/screens/variant_viewer_screen.h`

- [ ] **Step 1: Add `FilterMode` enum after `NetTypeTab`**

In `include/neuroflyer/ui/screens/variant_viewer_screen.h`, after line 17 (`enum class NetTypeTab { ... };`), insert:

```cpp
/// Filter applied to the Fighters tab variant list.
enum class FilterMode { All, SoloOnly, SquadOnly };
```

- [ ] **Step 2: Add `fighter_filter_` member variable**

In the same file, inside the `private:` section, after:
```cpp
    // Net type tab
    NetTypeTab active_tab_ = NetTypeTab::Fighters;
```
Add:
```cpp
    FilterMode fighter_filter_ = FilterMode::All;
```

- [ ] **Step 3: Verify the header compiles in isolation**

```bash
cd /Users/wldarden/repos/Neuroflyer/.claude/worktrees/solo-vs-squad-fighters
cmake --build build --target neuroflyer 2>&1 | head -30
```
Expected: no errors related to `variant_viewer_screen.h`.

- [ ] **Step 4: Commit**

```bash
git add include/neuroflyer/ui/screens/variant_viewer_screen.h
git commit -m "feat(ui): add FilterMode enum and member to VariantViewerScreen"
```

---

### Task 2: Render filter buttons in the Fighters tab

**Files:**
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`

- [ ] **Step 1: Add filter button row**

In `draw_variant_list`, inside the `if (active_tab_ == NetTypeTab::Fighters)` block, immediately **before** the `if (vs_.variants.empty())` check (currently line 471), insert:

```cpp
        // ---- Filter toggle: All | Solo | Squad ----
        {
            constexpr float FILTER_BTN_W = 60.0f;

            struct FilterBtn { const char* label; FilterMode mode; };
            constexpr FilterBtn FILTER_BTNS[] = {
                {"All",   FilterMode::All},
                {"Solo",  FilterMode::SoloOnly},
                {"Squad", FilterMode::SquadOnly},
            };

            for (const auto& fb : FILTER_BTNS) {
                bool is_active = (fighter_filter_ == fb.mode);
                if (ui::button(fb.label,
                        is_active ? ui::ButtonStyle::Primary
                                  : ui::ButtonStyle::Secondary,
                        FILTER_BTN_W)) {
                    if (!is_active) {
                        fighter_filter_ = fb.mode;
                        // Reset selection if current selection is no longer visible
                        if (!vs_.variants.empty() && vs_.selected_idx >= 0 &&
                            static_cast<std::size_t>(vs_.selected_idx) <
                                vs_.variants.size()) {
                            const auto& sel = vs_.variants[
                                static_cast<std::size_t>(vs_.selected_idx)];
                            bool still_visible =
                                (fb.mode == FilterMode::All) ||
                                (fb.mode == FilterMode::SoloOnly &&
                                    sel.net_type == NetType::Solo) ||
                                (fb.mode == FilterMode::SquadOnly &&
                                    sel.net_type == NetType::Fighter);
                            if (!still_visible) {
                                vs_.selected_idx = -1;
                                for (int i = 0;
                                     i < static_cast<int>(vs_.variants.size());
                                     ++i) {
                                    const auto& v = vs_.variants[
                                        static_cast<std::size_t>(i)];
                                    bool visible =
                                        (fb.mode == FilterMode::All) ||
                                        (fb.mode == FilterMode::SoloOnly &&
                                            v.net_type == NetType::Solo) ||
                                        (fb.mode == FilterMode::SquadOnly &&
                                            v.net_type == NetType::Fighter);
                                    if (visible) {
                                        vs_.selected_idx = i;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::Dummy(ImVec2(0, 4));
        }
```

- [ ] **Step 2: Build to catch compile errors**

```bash
cmake --build build --target neuroflyer 2>&1 | grep -E "error:|warning:" | head -20
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/ui/screens/hangar/variant_viewer_screen.cpp
git commit -m "feat(ui): add filter toggle buttons to Fighters tab"
```

---

### Task 3: Add type badges and filtered row loop

**Files:**
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`

- [ ] **Step 1: Replace the fighter variant table loop**

In `draw_variant_list`, replace the entire `if (ImGui::BeginTable("##VarTable", ...))` block (currently lines 475–535) with:

```cpp
            // Build filtered index list
            std::vector<int> filtered_indices;
            filtered_indices.reserve(vs_.variants.size());
            for (int i = 0;
                 i < static_cast<int>(vs_.variants.size()); ++i) {
                const auto& v =
                    vs_.variants[static_cast<std::size_t>(i)];
                if (fighter_filter_ == FilterMode::All ||
                    (fighter_filter_ == FilterMode::SoloOnly &&
                        v.net_type == NetType::Solo) ||
                    (fighter_filter_ == FilterMode::SquadOnly &&
                        v.net_type == NetType::Fighter)) {
                    filtered_indices.push_back(i);
                }
            }

            if (filtered_indices.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "(no variants match filter)");
            } else if (ImGui::BeginTable("##VarTable", 5,
                    ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                    ImVec2(0, content_h - 100.0f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name",
                    ImGuiTableColumnFlags_None, 0.30f);
                ImGui::TableSetupColumn("Gen",
                    ImGuiTableColumnFlags_None, 0.10f);
                ImGui::TableSetupColumn("Runs",
                    ImGuiTableColumnFlags_None, 0.10f);
                ImGui::TableSetupColumn("Created",
                    ImGuiTableColumnFlags_None, 0.18f);
                ImGui::TableSetupColumn("Parent",
                    ImGuiTableColumnFlags_None, 0.32f);
                ImGui::TableHeadersRow();

                for (int fi = 0;
                     fi < static_cast<int>(filtered_indices.size()); ++fi) {
                    const int i = filtered_indices[
                        static_cast<std::size_t>(fi)];
                    const auto& v =
                        vs_.variants[static_cast<std::size_t>(i)];
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();

                    // Type badge
                    if (v.net_type == NetType::Fighter) {
                        ImGui::TextColored(
                            ImVec4(0.64f, 0.61f, 0.99f, 1.0f), "[SQUAD]");
                    } else {
                        ImGui::TextColored(
                            ImVec4(0.0f, 0.82f, 0.83f, 1.0f), "[SOLO]");
                    }
                    ImGui::SameLine();

                    bool is_selected = (vs_.selected_idx == i);
                    if (ImGui::Selectable(v.name.c_str(), is_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
                        vs_.selected_idx = i;
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text("%u", v.generation);

                    ImGui::TableNextColumn();
                    if (v.run_count > 0) {
                        ImGui::Text("%u", v.run_count);
                    } else {
                        ImGui::TextColored(
                            ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "0");
                    }

                    ImGui::TableNextColumn();
                    if (v.created_timestamp > 0) {
                        ImGui::TextColored(
                            ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                            format_short_date(v.created_timestamp).c_str());
                    } else {
                        ImGui::TextColored(
                            ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "---");
                    }

                    ImGui::TableNextColumn();
                    if (v.parent_name.empty()) {
                        ImGui::TextColored(
                            ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "(root)");
                    } else {
                        ImGui::Text("%s", v.parent_name.c_str());
                    }
                }

                ImGui::EndTable();
            }
```

- [ ] **Step 2: Build**

```bash
cmake --build build --target neuroflyer 2>&1 | grep -E "error:|warning:" | head -20
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/ui/screens/hangar/variant_viewer_screen.cpp
git commit -m "feat(ui): add [SOLO]/[SQUAD] badges and filtered row loop to Fighters tab"
```

---

### Task 4: Reset filter on genome switch + full build

**Files:**
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`

- [ ] **Step 1: Reset `fighter_filter_` in the genome-change block**

In `on_draw`, inside the `if (state.active_genome != last_genome_)` block (currently lines 957–971), after the existing reset statements (after `paired_fighter_name_.clear();`), add:

```cpp
        fighter_filter_ = FilterMode::All;
```

The block should now look like:
```cpp
    if (state.active_genome != last_genome_) {
        last_genome_ = state.active_genome;
        vs_ = {};
        vs_.genome_name = state.active_genome;
        vs_.genome_dir =
            state.data_dir + "/genomes/" + state.active_genome;
        state.variants_dirty = true;
        state.lineage_dirty = true;
        evo_loaded_ = false;
        sub_view_ = SubView::List;
        // Reset squad state for new genome
        squad_variants_.clear();
        squad_selected_idx_ = 0;
        paired_fighter_name_.clear();
        fighter_filter_ = FilterMode::All;
    }
```

- [ ] **Step 2: Full build**

```bash
cmake --build build --target neuroflyer 2>&1 | grep -E "error:|warning:" | head -20
```
Expected: no errors, no warnings.

- [ ] **Step 3: Manual smoke test**

Run `./build/neuroflyer` and:
1. Open a genome in the Hangar → Variant Viewer.
2. Confirm the Fighters tab shows `[SOLO]` (cyan) or `[SQUAD]` (purple) badges on each variant row.
3. Click **Solo** filter — confirm only `[SOLO]` variants are shown.
4. Click **Squad** filter — confirm only `[SQUAD]` variants are shown (or "(no variants match filter)" if none exist).
5. Click **All** — confirm all variants reappear.
6. With a variant selected, switch filter so it is excluded — confirm selection resets to the first visible row (not -1 unless the list is empty).
7. Switch to a different genome — confirm filter resets to **All**.

- [ ] **Step 4: Commit**

```bash
git add src/ui/screens/hangar/variant_viewer_screen.cpp
git commit -m "feat(ui): reset fighter_filter_ on genome switch in VariantViewerScreen"
```
