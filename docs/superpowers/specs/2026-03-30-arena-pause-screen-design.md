# Arena Pause Screen — Design Spec

**Date:** 2026-03-30

## Overview

An arena mode pause screen that lets users save evolved squad net variants to disk. Pushed from `ArenaGameScreen` on Space, popped on Escape or Resume. Contains a single "Save Squad Variants" panel showing teams ranked by fitness with multi-select checkboxes.

## Screen: ArenaPauseScreen

### Constructor Data

The screen receives read-only data copied from `ArenaGameScreen`:

- `std::vector<TeamIndividual> team_population` — the current evolved teams (copied, not referenced, since arena owns the originals)
- `std::size_t generation` — current generation number
- `ShipDesign ship_design` — for snapshot construction
- `std::string genome_dir` — save target (e.g., `data/genomes/ArenaFighter/`)
- `std::string paired_fighter_name` — the fighter variant this squad was trained with
- `NtmNetConfig ntm_config` — NTM topology info for snapshot construction

### Save Panel Layout

**Left panel (~60% width):** Team list sorted by fitness descending.
- Each row: checkbox, rank number, fitness score (integer)
- "All / Clear" toggle buttons at the top
- Top N teams visually distinguished (gold text for elites matching `evo_config_.elitism_count`)

**Right panel (~40% width):** Summary info.
- Generation number
- Team count
- Paired fighter name (if squad training mode)
- Number of selected teams

**Bottom bar:**
- "Save N Selected" button — disabled when nothing selected
- "Resume" button — pops the screen (same as Escape)

### Save Flow

When "Save N Selected" is clicked:

1. Open `InputModal` with pre-filled base name `"gen{generation}"`
2. On confirm, for each selected team index `i` (0-based rank by fitness):
   - **Squad leader snapshot:**
     - `snap.name = "{base}-{i}"`
     - `snap.net_type = NetType::SquadLeader`
     - `snap.generation = generation`
     - `snap.ship_design = ship_design`
     - `snap.topology = team.squad_individual.topology`
     - `snap.weights = team.squad_individual.genome.flatten("layer_")`
     - `snap.paired_fighter_name = paired_fighter_name`
     - `snap.parent_name = ""` (no lineage tracking for now)
     - `snap.created_timestamp = now()`
     - Save via `save_squad_variant(genome_dir, snap)`
   - **NTM companion snapshot:**
     - `snap.name = "{base}-{i}-ntm"`
     - `snap.net_type = NetType::Solo` (reuse existing type)
     - `snap.generation = generation`
     - `snap.ship_design = ship_design` (not functionally used for NTM but required by format)
     - `snap.topology = team.ntm_individual.topology`
     - `snap.weights = team.ntm_individual.genome.flatten("layer_")`
     - `snap.paired_fighter_name = ""`
     - `snap.parent_name = ""`
     - `snap.created_timestamp = now()`
     - Save via `save_squad_variant(genome_dir, ntm_snap)`
3. Print confirmation to console: `"Saved N squad variants to {genome_dir}/squad/"`

### ArenaGameScreen Changes

Currently, Space toggles `paused_` locally. Change to:

```
if (Space pressed) {
    paused_ = true;
    push ArenaPauseScreen with current team_population, generation, etc.
}
```

When `ArenaPauseScreen` is popped (Resume/Escape), `ArenaGameScreen::on_draw()` resumes because `paused_` needs to be unset. Two options:

**Option A:** `ArenaPauseScreen` sets `paused_ = false` on the arena screen via a callback.

**Option B:** `ArenaGameScreen` detects the pause screen was popped (screen stack returns to it) and unsets `paused_` in `on_draw()`.

**Choice: Option B.** On each `on_draw()`, if `paused_` is true but no screen is on top (i.e., this screen is the active one), unpause. This is simpler and avoids coupling. Actually, even simpler: just unpause when `on_draw()` is called and `paused_` is true — if we're drawing, the pause screen has been popped. But `on_draw()` is called every frame including while paused (that's how we render the game behind the pause screen).

**Revised approach:** Don't use `paused_` at all for the screen-based pause. The existing tick loop already skips ticks when `paused_` is true. Just set `paused_ = true` before pushing, and set it back to `false` after the push returns... but push is not blocking.

**Simplest approach:** Keep `paused_` toggling as-is. Space sets `paused_ = true` and pushes `ArenaPauseScreen`. The pause screen's Resume/Escape sets a flag or calls a lambda to unpause. Pass a `std::function<void()> on_resume` callback to the constructor.

```cpp
ArenaPauseScreen(
    ...,
    std::function<void()> on_resume  // called when screen is popped
);
```

In the pause screen's Resume/Escape handler:
```cpp
on_resume_();
ui.pop_screen();
```

In `ArenaGameScreen`:
```cpp
if (Space) {
    paused_ = true;
    ui.push_screen(std::make_unique<ArenaPauseScreen>(
        team_population_, generation_, ship_design_,
        genome_dir, paired_fighter_name_, ntm_config_,
        [this]() { paused_ = false; }
    ));
}
```

## Edge Cases

- **No genome_dir available:** In non-squad-training mode, there may not be a genome directory. The save button should be disabled or hidden if `genome_dir` is empty.
- **Empty team population:** Should not happen in practice (arena always has teams), but guard against it.
- **Save during evolution:** The team population is copied into the pause screen, so evolution cannot modify it while the user is selecting.

## Files

| File | Change |
|---|---|
| `include/neuroflyer/ui/screens/arena_pause_screen.h` | New — ArenaPauseScreen class |
| `src/ui/screens/arena/arena_pause_screen.cpp` | New — implementation |
| `src/ui/screens/arena/arena_game_screen.cpp` | Modify Space handler to push ArenaPauseScreen |
| `CMakeLists.txt` | Add new source file |
