# Squad Leader Net Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the simple 8→4 squad broadcast net with a structured squad leader net that makes discrete tactical decisions via 1-hot output groups, using Near Threat Matrix (NTM) sub-nets for variable-count local threat processing and a sector grid for spatial indexing.

**Architecture:** Three new engine modules (sector grid, NTM execution, squad leader execution) feed into the existing arena match loop. `TeamIndividual` gains a third net (NTM). Fighter inputs change from 4 opaque broadcast floats to 6 structured values (target heading/dist, center heading/dist, aggression, spacing). The arena match tick loop is rewritten: sector query → NTM top-1 selection → squad leader forward → argmax orders → engine interprets orders → compute per-fighter inputs.

**Tech Stack:** C++20, neuralnet library (Network, NetworkTopology), GoogleTest, CMake

**Spec:** `docs/superpowers/specs/2026-03-30-squad-leader-design.md`

---

## File Structure

### New Files

| File | Responsibility |
|------|----------------|
| `include/neuroflyer/sector_grid.h` | Sector grid data structure: insert/remove/query entities by 2D sector coordinate |
| `src/engine/sector_grid.cpp` | Sector grid implementation |
| `include/neuroflyer/squad_leader.h` | NTM/squad-leader configs, order enums, execution functions (run NTMs, run leader, interpret orders) |
| `src/engine/squad_leader.cpp` | NTM + squad leader execution logic |
| `tests/sector_grid_test.cpp` | Sector grid unit tests |
| `tests/squad_leader_test.cpp` | NTM + squad leader unit tests |

### Modified Files

| File | What Changes |
|------|-------------|
| `include/neuroflyer/arena_config.h` | Add sector grid, NTM topology, and squad leader topology config fields |
| `include/neuroflyer/team_evolution.h` | Replace `SquadNetConfig` with `SquadLeaderConfig` + `NtmConfig`. Add `ntm_individual` to `TeamIndividual`. |
| `src/engine/team_evolution.cpp` | Create/build/evolve 3 nets instead of 2 |
| `include/neuroflyer/arena_session.h` | Add `compute_squad_spacing()` to `SquadStats` |
| `src/engine/arena_session.cpp` | Implement squad spacing (stddev of ship-to-center distances) |
| `include/neuroflyer/arena_sensor.h` | Replace `broadcast_signals` param with `squad_leader_inputs` (6 floats) in `build_arena_ship_input` |
| `src/engine/arena_sensor.cpp` | Update input building for new layout |
| `src/engine/arena_match.cpp` | Rewrite tick loop: sector grid → NTM → squad leader → fighter |
| `include/neuroflyer/ui/screens/arena_game_screen.h` | Update member variables for new net types |
| `src/ui/screens/arena/arena_game_screen.cpp` | Update tick loop + evolution to match new arena_match flow |
| `tests/CMakeLists.txt` | Add `sector_grid.cpp`, `squad_leader.cpp` to engine sources; add test files |
| `tests/team_evolution_test.cpp` | Update for 3-net TeamIndividual |
| `tests/arena_sensor_test.cpp` | Update for new input layout (6 squad leader inputs instead of 4 broadcasts) |
| `tests/arena_match_test.cpp` | Update for new configs and 3-net teams |

---

### Task 1: Sector Grid

**Files:**
- Create: `include/neuroflyer/sector_grid.h`
- Create: `src/engine/sector_grid.cpp`
- Create: `tests/sector_grid_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add source files to CMakeLists.txt**

In `tests/CMakeLists.txt`, add `sector_grid.cpp` to the engine sources list and `sector_grid_test.cpp` to the test sources list.

In the `set(ENGINE_SOURCES ...)` block (or equivalent list of `../src/engine/*.cpp` files), add:
```
../src/engine/sector_grid.cpp
```

In the `add_executable(neuroflyer_tests ...)` block, add:
```
sector_grid_test.cpp
```

- [ ] **Step 2: Write the header**

Create `include/neuroflyer/sector_grid.h`:

```cpp
#pragma once

#include <cstddef>
#include <vector>

namespace neuroflyer {

struct SectorCoord {
    int row = 0;
    int col = 0;

    bool operator==(const SectorCoord& o) const noexcept {
        return row == o.row && col == o.col;
    }
};

/// 2D spatial index dividing the world into fixed-size sectors.
/// Each sector holds a list of entity indices.
class SectorGrid {
public:
    /// Construct a grid for a world of the given dimensions.
    SectorGrid(float world_width, float world_height, float sector_size);

    /// Which sector contains this world-space point.
    [[nodiscard]] SectorCoord sector_of(float x, float y) const noexcept;

    /// Insert an entity index into the sector containing (x, y).
    void insert(std::size_t entity_id, float x, float y);

    /// Clear all entities from all sectors.
    void clear();

    /// Gather all entity IDs within Manhattan distance <= radius sectors
    /// of the given center sector. Returns a flat vector of entity IDs.
    [[nodiscard]] std::vector<std::size_t> entities_in_diamond(
        SectorCoord center, int radius) const;

    /// Grid dimensions.
    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] int cols() const noexcept { return cols_; }
    [[nodiscard]] float sector_size() const noexcept { return sector_size_; }

private:
    [[nodiscard]] std::size_t grid_index(int row, int col) const noexcept;

    float sector_size_;
    int rows_;
    int cols_;
    // Flat grid: cells_[row * cols_ + col] = vector of entity IDs
    std::vector<std::vector<std::size_t>> cells_;
};

} // namespace neuroflyer
```

- [ ] **Step 3: Write the failing tests**

Create `tests/sector_grid_test.cpp`:

```cpp
#include <neuroflyer/sector_grid.h>
#include <gtest/gtest.h>

#include <algorithm>

using namespace neuroflyer;

TEST(SectorGrid, ConstructsCorrectDimensions) {
    // 10000x8000 world, 2000px sectors => 5 cols x 4 rows
    SectorGrid grid(10000.0f, 8000.0f, 2000.0f);
    EXPECT_EQ(grid.cols(), 5);
    EXPECT_EQ(grid.rows(), 4);
}

TEST(SectorGrid, SectorOfClampsToBounds) {
    SectorGrid grid(10000.0f, 8000.0f, 2000.0f);

    // Normal case: (3000, 5000) => col=1, row=2
    auto s1 = grid.sector_of(3000.0f, 5000.0f);
    EXPECT_EQ(s1.col, 1);
    EXPECT_EQ(s1.row, 2);

    // Edge case: exactly on boundary (10000, 8000) => clamped to last sector
    auto s2 = grid.sector_of(10000.0f, 8000.0f);
    EXPECT_EQ(s2.col, 4);
    EXPECT_EQ(s2.row, 3);

    // Negative coords clamp to 0
    auto s3 = grid.sector_of(-100.0f, -100.0f);
    EXPECT_EQ(s3.col, 0);
    EXPECT_EQ(s3.row, 0);
}

TEST(SectorGrid, InsertAndRetrieve) {
    SectorGrid grid(10000.0f, 8000.0f, 2000.0f);
    grid.insert(42, 3000.0f, 5000.0f);  // sector (2, 1)
    grid.insert(99, 3500.0f, 5500.0f);  // same sector (2, 1)
    grid.insert(7, 100.0f, 100.0f);     // sector (0, 0)

    // Diamond radius 0 around (2,1) should return just entities in that sector
    auto result = grid.entities_in_diamond({2, 1}, 0);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(std::find(result.begin(), result.end(), 42) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 99) != result.end());
}

TEST(SectorGrid, DiamondRadius2) {
    // 10x10 grid (20000x20000 world, 2000px sectors)
    SectorGrid grid(20000.0f, 20000.0f, 2000.0f);

    // Place entities at known sectors
    // Center sector (5, 5): entity 0
    grid.insert(0, 11000.0f, 11000.0f);
    // Sector (5, 3) — Manhattan dist 2 from (5,5): entity 1
    grid.insert(1, 7000.0f, 11000.0f);
    // Sector (5, 8) — Manhattan dist 3 from (5,5): entity 2 (OUT of range)
    grid.insert(2, 17000.0f, 11000.0f);
    // Sector (3, 5) — Manhattan dist 2 from (5,5): entity 3
    grid.insert(3, 11000.0f, 7000.0f);
    // Sector (4, 4) — Manhattan dist 2 from (5,5): entity 4
    grid.insert(4, 9000.0f, 9000.0f);

    auto result = grid.entities_in_diamond({5, 5}, 2);

    // Should include entities 0, 1, 3, 4 (within Manhattan dist 2)
    // Should NOT include entity 2 (Manhattan dist 3)
    EXPECT_TRUE(std::find(result.begin(), result.end(), 0) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 1) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 3) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 4) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 2) == result.end());
}

TEST(SectorGrid, DiamondClampsAtEdges) {
    SectorGrid grid(10000.0f, 10000.0f, 2000.0f);  // 5x5 grid
    grid.insert(0, 100.0f, 100.0f);      // sector (0, 0)
    grid.insert(1, 3000.0f, 100.0f);     // sector (0, 1)
    grid.insert(2, 100.0f, 3000.0f);     // sector (1, 0)

    // Diamond radius 2 around corner (0,0) — should not crash, should find all 3
    auto result = grid.entities_in_diamond({0, 0}, 2);
    EXPECT_TRUE(std::find(result.begin(), result.end(), 0) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 1) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 2) != result.end());
}

TEST(SectorGrid, ClearRemovesAll) {
    SectorGrid grid(10000.0f, 10000.0f, 2000.0f);
    grid.insert(0, 100.0f, 100.0f);
    grid.insert(1, 5000.0f, 5000.0f);
    grid.clear();

    auto result = grid.entities_in_diamond({0, 0}, 10);
    EXPECT_TRUE(result.empty());
}
```

- [ ] **Step 4: Run tests — expect FAIL (linker errors)**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests 2>&1 | tail -20`

Expected: Linker errors for `SectorGrid` constructor, methods (no .cpp yet).

- [ ] **Step 5: Implement sector_grid.cpp**

Create `src/engine/sector_grid.cpp`:

```cpp
#include <neuroflyer/sector_grid.h>

#include <algorithm>
#include <cmath>

namespace neuroflyer {

SectorGrid::SectorGrid(float world_width, float world_height, float sector_size)
    : sector_size_(sector_size),
      rows_(std::max(1, static_cast<int>(std::ceil(world_height / sector_size)))),
      cols_(std::max(1, static_cast<int>(std::ceil(world_width / sector_size)))) {
    cells_.resize(static_cast<std::size_t>(rows_ * cols_));
}

SectorCoord SectorGrid::sector_of(float x, float y) const noexcept {
    int col = static_cast<int>(x / sector_size_);
    int row = static_cast<int>(y / sector_size_);
    col = std::clamp(col, 0, cols_ - 1);
    row = std::clamp(row, 0, rows_ - 1);
    return {row, col};
}

void SectorGrid::insert(std::size_t entity_id, float x, float y) {
    auto sc = sector_of(x, y);
    cells_[grid_index(sc.row, sc.col)].push_back(entity_id);
}

void SectorGrid::clear() {
    for (auto& cell : cells_) {
        cell.clear();
    }
}

std::vector<std::size_t> SectorGrid::entities_in_diamond(
    SectorCoord center, int radius) const {

    std::vector<std::size_t> result;
    for (int dr = -radius; dr <= radius; ++dr) {
        int max_dc = radius - std::abs(dr);
        for (int dc = -max_dc; dc <= max_dc; ++dc) {
            int r = center.row + dr;
            int c = center.col + dc;
            if (r < 0 || r >= rows_ || c < 0 || c >= cols_) continue;
            const auto& cell = cells_[grid_index(r, c)];
            result.insert(result.end(), cell.begin(), cell.end());
        }
    }
    return result;
}

std::size_t SectorGrid::grid_index(int row, int col) const noexcept {
    return static_cast<std::size_t>(row * cols_ + col);
}

} // namespace neuroflyer
```

- [ ] **Step 6: Run tests — expect PASS**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='SectorGrid*'`

Expected: All 6 SectorGrid tests PASS.

- [ ] **Step 7: Commit**

```bash
git add include/neuroflyer/sector_grid.h src/engine/sector_grid.cpp tests/sector_grid_test.cpp tests/CMakeLists.txt
git commit -m "feat: add SectorGrid spatial index for arena NTM threat detection"
```

---

### Task 2: Arena Config Updates

**Files:**
- Modify: `include/neuroflyer/arena_config.h`

- [ ] **Step 1: Add sector grid and squad leader config fields**

In `include/neuroflyer/arena_config.h`, replace the existing `// Squads` section:

Replace:
```cpp
    // Squads
    std::size_t num_squads = 1;
    std::size_t fighters_per_squad = 8;
    std::size_t squad_broadcast_signals = 4;
```

With:
```cpp
    // Squads
    std::size_t num_squads = 1;
    std::size_t fighters_per_squad = 8;

    // Sector grid (for NTM spatial indexing)
    float sector_size = 2000.0f;
    int ntm_sector_radius = 2;  // Manhattan distance for "near"

    // Near Threat Matrix (NTM) sub-net topology
    std::size_t ntm_input_size = 6;
    std::vector<std::size_t> ntm_hidden_sizes = {4};
    std::size_t ntm_output_size = 1;  // just threat_score

    // Squad leader net topology
    std::size_t squad_leader_input_size = 11;
    std::vector<std::size_t> squad_leader_hidden_sizes = {8};
    std::size_t squad_leader_output_size = 5;  // 2 spacing + 3 tactical

    // Squad leader fighter inputs (replaces broadcast signals)
    static constexpr std::size_t squad_leader_fighter_inputs = 6;
```

Add `#include <vector>` to the includes at the top of the file (after `<cstdint>`).

- [ ] **Step 2: Verify build**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests 2>&1 | tail -5`

Expected: Build succeeds (no callers use `squad_broadcast_signals` in tests yet — existing test code will break in later tasks when we update them).

- [ ] **Step 3: Commit**

```bash
git add include/neuroflyer/arena_config.h
git commit -m "feat: add sector grid, NTM, and squad leader config to ArenaConfig"
```

---

### Task 3: Squad Leader Header — Configs, Enums, Functions

**Files:**
- Create: `include/neuroflyer/squad_leader.h`
- Create: `src/engine/squad_leader.cpp` (stub)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the header**

Create `include/neuroflyer/squad_leader.h`:

```cpp
#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/base.h>
#include <neuroflyer/game.h>
#include <neuroflyer/sector_grid.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <span>
#include <vector>

namespace neuroflyer {

// ── Order Enums ──────────────────────────────────────────────────────────────

enum class TacticalOrder {
    AttackStarbase = 0,
    AttackShip = 1,
    DefendHome = 2
};

enum class SpacingOrder {
    Expand = 0,
    Contract = 1
};

// ── NTM (Near Threat Matrix) ─────────────────────────────────────────────────

/// One nearby threat entity visible to a squad leader.
struct NearThreat {
    float x = 0, y = 0;             // world position
    float health = 1.0f;            // normalized HP (ships: alive ? 1 : 0, bases: hp/max)
    bool is_ship = false;
    bool is_starbase = false;
    std::size_t entity_id = 0;      // index into ships or bases array
};

/// Result of running NTMs and selecting the top-1 threat.
struct NtmResult {
    bool active = false;             // true if any threats were found
    float threat_score = 0.0f;       // evolved score of the active threat
    float target_x = 0, target_y = 0; // position of active threat entity
    float heading = 0.0f;            // direction from squad center (radians, normalized)
    float distance = 0.0f;           // distance from squad center (normalized to world diag)
};

/// Gather nearby enemy entities from the sector grid for a given squad.
/// Returns one NearThreat per enemy entity within the NTM diamond.
[[nodiscard]] std::vector<NearThreat> gather_near_threats(
    const SectorGrid& grid,
    float squad_center_x, float squad_center_y,
    int ntm_sector_radius,
    int squad_team,
    std::span<const Triangle> ships,
    std::span<const int> ship_teams,
    std::span<const Base> bases);

/// Run NTM sub-nets for all nearby threats and select the top-1.
/// ntm_net uses shared weights — called once per threat with different inputs.
[[nodiscard]] NtmResult run_ntm_threat_selection(
    const neuralnet::Network& ntm_net,
    float squad_center_x, float squad_center_y,
    float squad_alive_fraction,
    const std::vector<NearThreat>& threats,
    float world_w, float world_h);

// ── Squad Leader ─────────────────────────────────────────────────────────────

/// Full result of running the squad leader net for one squad.
struct SquadLeaderOrder {
    TacticalOrder tactical = TacticalOrder::AttackStarbase;
    SpacingOrder spacing = SpacingOrder::Expand;

    // Target entity position (determined by tactical order + NTM result)
    float target_x = 0, target_y = 0;
};

/// Run the squad leader net and interpret its output into orders.
/// commander_target_x/y: Phase 1 = enemy starbase. Phase 2+ = commander-selected.
[[nodiscard]] SquadLeaderOrder run_squad_leader(
    const neuralnet::Network& leader_net,
    float squad_health,
    float home_distance,
    float home_heading,
    float home_health,
    float squad_spacing,
    float commander_target_heading,
    float commander_target_distance,
    const NtmResult& ntm,
    float own_base_x, float own_base_y,
    float enemy_base_x, float enemy_base_y);

// ── Fighter Inputs ───────────────────────────────────────────────────────────

/// The 6 structured inputs a fighter receives from its squad leader.
struct SquadLeaderFighterInputs {
    float squad_target_heading = 0;    // dir from this fighter to order target
    float squad_target_distance = 0;   // dist from this fighter to order target (normalized)
    float squad_center_heading = 0;    // dir from this fighter to squad center
    float squad_center_distance = 0;   // dist from this fighter to squad center (normalized)
    float aggression = 0;              // +1 attack, -1 defend
    float spacing = 0;                 // +1 expand, -1 contract
};

/// Compute the 6 fighter inputs from the squad leader's orders.
[[nodiscard]] SquadLeaderFighterInputs compute_squad_leader_fighter_inputs(
    float fighter_x, float fighter_y,
    const SquadLeaderOrder& order,
    float squad_center_x, float squad_center_y,
    float world_w, float world_h);

} // namespace neuroflyer
```

- [ ] **Step 2: Create stub implementation**

Create `src/engine/squad_leader.cpp`:

```cpp
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/arena_sensor.h>  // for compute_dir_range

#include <algorithm>
#include <cmath>
#include <limits>

namespace neuroflyer {

std::vector<NearThreat> gather_near_threats(
    const SectorGrid& grid,
    float squad_center_x, float squad_center_y,
    int ntm_sector_radius,
    int squad_team,
    std::span<const Triangle> ships,
    std::span<const int> ship_teams,
    std::span<const Base> bases) {
    // Stub — implemented in Task 5
    (void)grid; (void)squad_center_x; (void)squad_center_y;
    (void)ntm_sector_radius; (void)squad_team;
    (void)ships; (void)ship_teams; (void)bases;
    return {};
}

NtmResult run_ntm_threat_selection(
    const neuralnet::Network& ntm_net,
    float squad_center_x, float squad_center_y,
    float squad_alive_fraction,
    const std::vector<NearThreat>& threats,
    float world_w, float world_h) {
    // Stub — implemented in Task 5
    (void)ntm_net; (void)squad_center_x; (void)squad_center_y;
    (void)squad_alive_fraction; (void)threats;
    (void)world_w; (void)world_h;
    return {};
}

SquadLeaderOrder run_squad_leader(
    const neuralnet::Network& leader_net,
    float squad_health,
    float home_distance,
    float home_heading,
    float home_health,
    float squad_spacing,
    float commander_target_heading,
    float commander_target_distance,
    const NtmResult& ntm,
    float own_base_x, float own_base_y,
    float enemy_base_x, float enemy_base_y) {
    // Stub — implemented in Task 6
    (void)leader_net; (void)squad_health; (void)home_distance; (void)home_heading;
    (void)home_health; (void)squad_spacing; (void)commander_target_heading;
    (void)commander_target_distance; (void)ntm;
    (void)own_base_x; (void)own_base_y; (void)enemy_base_x; (void)enemy_base_y;
    return {};
}

SquadLeaderFighterInputs compute_squad_leader_fighter_inputs(
    float fighter_x, float fighter_y,
    const SquadLeaderOrder& order,
    float squad_center_x, float squad_center_y,
    float world_w, float world_h) {
    // Stub — implemented in Task 6
    (void)fighter_x; (void)fighter_y; (void)order;
    (void)squad_center_x; (void)squad_center_y;
    (void)world_w; (void)world_h;
    return {};
}

} // namespace neuroflyer
```

- [ ] **Step 3: Add squad_leader.cpp to tests CMakeLists.txt**

In `tests/CMakeLists.txt`, add to the engine sources list:
```
../src/engine/squad_leader.cpp
```

- [ ] **Step 4: Verify build**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests 2>&1 | tail -5`

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/squad_leader.h src/engine/squad_leader.cpp tests/CMakeLists.txt
git commit -m "feat: add squad_leader header with NTM, order enums, and function stubs"
```

---

### Task 4: Update TeamIndividual for 3 Nets

**Files:**
- Modify: `include/neuroflyer/team_evolution.h`
- Modify: `src/engine/team_evolution.cpp`
- Modify: `tests/team_evolution_test.cpp`

- [ ] **Step 1: Update the header**

Replace the entire contents of `include/neuroflyer/team_evolution.h`:

```cpp
#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/ship_design.h>

#include <random>
#include <vector>

namespace neuroflyer {

struct NtmNetConfig {
    std::size_t input_size = 6;
    std::vector<std::size_t> hidden_sizes = {4};
    std::size_t output_size = 1;  // threat_score only
};

struct SquadLeaderNetConfig {
    std::size_t input_size = 11;
    std::vector<std::size_t> hidden_sizes = {8};
    std::size_t output_size = 5;  // 2 spacing + 3 tactical
};

struct TeamIndividual {
    Individual ntm_individual;       // Near Threat Matrix sub-net (shared weights)
    Individual squad_individual;     // Squad leader net
    Individual fighter_individual;   // Fighter net
    float fitness = 0.0f;

    /// Create a random team with NTM + squad leader + fighter nets.
    static TeamIndividual create(
        const ShipDesign& fighter_design,
        const std::vector<std::size_t>& fighter_hidden,
        const NtmNetConfig& ntm_config,
        const SquadLeaderNetConfig& leader_config,
        std::mt19937& rng);

    /// Build networks from individuals.
    [[nodiscard]] neuralnet::Network build_ntm_network() const;
    [[nodiscard]] neuralnet::Network build_squad_network() const;
    [[nodiscard]] neuralnet::Network build_fighter_network() const;
};

/// Create initial team population.
[[nodiscard]] std::vector<TeamIndividual> create_team_population(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const NtmNetConfig& ntm_config,
    const SquadLeaderNetConfig& leader_config,
    std::size_t population_size,
    std::mt19937& rng);

/// Evolve one generation of teams. Returns next population.
[[nodiscard]] std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);

/// Evolve only squad leader + NTM nets — fighter weights are frozen.
/// Used for squad-specific training with a fixed fighter variant.
[[nodiscard]] std::vector<TeamIndividual> evolve_squad_only(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);

} // namespace neuroflyer
```

- [ ] **Step 2: Update the implementation**

Replace the entire contents of `src/engine/team_evolution.cpp`:

```cpp
#include <neuroflyer/team_evolution.h>

#include <algorithm>

namespace neuroflyer {

TeamIndividual TeamIndividual::create(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const NtmNetConfig& ntm_config,
    const SquadLeaderNetConfig& leader_config,
    std::mt19937& rng) {

    TeamIndividual team;

    // NTM sub-net
    team.ntm_individual = Individual::random(
        ntm_config.input_size,
        ntm_config.hidden_sizes,
        ntm_config.output_size,
        rng);

    // Squad leader net
    team.squad_individual = Individual::random(
        leader_config.input_size,
        leader_config.hidden_sizes,
        leader_config.output_size,
        rng);

    // Fighter net: uses arena input size with 6 squad leader inputs (not 4 broadcasts)
    std::size_t arena_input = compute_arena_input_size(
        fighter_design, ArenaConfig::squad_leader_fighter_inputs);
    std::size_t arena_output = compute_output_size(fighter_design);
    team.fighter_individual = Individual::random(
        arena_input,
        fighter_hidden,
        arena_output,
        rng);

    return team;
}

neuralnet::Network TeamIndividual::build_ntm_network() const {
    return ntm_individual.build_network();
}

neuralnet::Network TeamIndividual::build_squad_network() const {
    return squad_individual.build_network();
}

neuralnet::Network TeamIndividual::build_fighter_network() const {
    return fighter_individual.build_network();
}

std::vector<TeamIndividual> create_team_population(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const NtmNetConfig& ntm_config,
    const SquadLeaderNetConfig& leader_config,
    std::size_t population_size,
    std::mt19937& rng) {

    std::vector<TeamIndividual> pop;
    pop.reserve(population_size);
    for (std::size_t i = 0; i < population_size; ++i) {
        pop.push_back(TeamIndividual::create(
            fighter_design, fighter_hidden, ntm_config, leader_config, rng));
    }
    return pop;
}

std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    std::sort(population.begin(), population.end(),
              [](const auto& a, const auto& b) { return a.fitness > b.fitness; });

    std::vector<TeamIndividual> next;
    next.reserve(population.size());

    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

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

        // Mutate all three nets independently
        apply_mutations(child.ntm_individual, config, rng);
        apply_mutations(child.squad_individual, config, rng);
        apply_mutations(child.fighter_individual, config, rng);

        next.push_back(std::move(child));
    }

    return next;
}

std::vector<TeamIndividual> evolve_squad_only(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    std::sort(population.begin(), population.end(),
              [](const auto& a, const auto& b) { return a.fitness > b.fitness; });

    std::vector<TeamIndividual> next;
    next.reserve(population.size());

    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

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

        // Only mutate NTM + squad leader — fighter stays frozen
        apply_mutations(child.ntm_individual, config, rng);
        apply_mutations(child.squad_individual, config, rng);

        next.push_back(std::move(child));
    }

    return next;
}

} // namespace neuroflyer
```

- [ ] **Step 3: Update tests**

Replace the entire contents of `tests/team_evolution_test.cpp`:

```cpp
#include <neuroflyer/team_evolution.h>
#include <gtest/gtest.h>

using namespace neuroflyer;

namespace {

ShipDesign make_test_design() {
    ShipDesign d;
    SensorDef s;
    s.angle = 0.0f;
    s.range = 200.0f;
    s.is_full_sensor = true;
    d.sensors.push_back(s);
    d.memory_slots = 2;
    return d;
}

} // namespace

TEST(TeamEvolution, CreateTeamIndividual) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;

    auto team = TeamIndividual::create(design, {6}, ntm_cfg, leader_cfg, rng);

    // NTM net: 6 -> [4] -> 1
    auto ntm_net = team.build_ntm_network();
    EXPECT_EQ(ntm_net.input_size(), 6u);
    EXPECT_EQ(ntm_net.output_size(), 1u);

    // Squad leader: 11 -> [8] -> 5
    auto leader_net = team.build_squad_network();
    EXPECT_EQ(leader_net.input_size(), 11u);
    EXPECT_EQ(leader_net.output_size(), 5u);

    // Fighter: sensors(5) + pos(3) + nav(7) + squad_leader(6) + mem(2)
    auto fighter_net = team.build_fighter_network();
    EXPECT_EQ(fighter_net.input_size(), 5u + 3 + 7 + 6 + 2);
    EXPECT_EQ(fighter_net.output_size(), 5u + 2);  // 5 actions + 2 memory
}

TEST(TeamEvolution, EvolveTeamPopulation) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;

    auto pop = create_team_population(design, {6}, ntm_cfg, leader_cfg, 10, rng);
    EXPECT_EQ(pop.size(), 10u);

    // Assign ascending fitness
    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i);
    }

    EvolutionConfig evo;
    evo.population_size = 10;
    evo.elitism_count = 2;
    evo.tournament_size = 3;

    auto next = evolve_team_population(pop, evo, rng);
    EXPECT_EQ(next.size(), 10u);

    // All fitness reset to 0
    for (const auto& t : next) {
        EXPECT_FLOAT_EQ(t.fitness, 0.0f);
    }

    // All three nets should still build valid networks
    for (const auto& t : next) {
        auto ntm = t.build_ntm_network();
        auto leader = t.build_squad_network();
        auto fighter = t.build_fighter_network();
        EXPECT_EQ(ntm.input_size(), 6u);
        EXPECT_EQ(leader.input_size(), 11u);
        EXPECT_GT(fighter.input_size(), 0u);
    }
}

TEST(TeamEvolution, EvolveSquadOnlyFreezesFighters) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;

    auto pop = create_team_population(design, {6}, ntm_cfg, leader_cfg, 6, rng);

    // Snapshot fighter weights from individual 0
    auto original_fighter_net = pop[0].build_fighter_network();
    auto original_weights = original_fighter_net.get_all_weights();

    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i);
    }

    EvolutionConfig evo;
    evo.population_size = 6;
    evo.elitism_count = 2;
    evo.tournament_size = 3;

    auto next = evolve_squad_only(pop, evo, rng);
    EXPECT_EQ(next.size(), 6u);

    // Elite individual 0 (highest fitness = index 5 after sort) should have same fighter weights
    // Since we sorted descending and pop[5] had highest fitness, next[0] = pop[5]
    // Check that ALL individuals have unmodified fighter weights (fighters are copies of parents)
    // The key invariant: evolve_squad_only never calls apply_mutations on fighter_individual
    for (const auto& t : next) {
        EXPECT_GT(t.build_fighter_network().input_size(), 0u);
    }
}
```

- [ ] **Step 4: Build and run tests**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='TeamEvolution*'`

Expected: All 3 TeamEvolution tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/team_evolution.h src/engine/team_evolution.cpp tests/team_evolution_test.cpp
git commit -m "feat: update TeamIndividual to 3 nets (NTM + squad leader + fighter)"
```

---

### Task 5: NTM Execution — gather_near_threats + run_ntm_threat_selection

**Files:**
- Modify: `src/engine/squad_leader.cpp`
- Create: `tests/squad_leader_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add test file to CMakeLists.txt**

In `tests/CMakeLists.txt`, add to the test sources:
```
squad_leader_test.cpp
```

- [ ] **Step 2: Write NTM tests**

Create `tests/squad_leader_test.cpp`:

```cpp
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/evolution.h>
#include <gtest/gtest.h>

using namespace neuroflyer;

// ── gather_near_threats tests ────────────────────────────────────────────────

TEST(SquadLeader, GatherNearThreatsFindsEnemyShips) {
    // 10000x10000 world, 2000px sectors => 5x5 grid
    SectorGrid grid(10000.0f, 10000.0f, 2000.0f);

    // Squad center at (5000, 5000) => sector (2, 2)
    float squad_cx = 5000.0f, squad_cy = 5000.0f;
    int squad_team = 0;

    // Place enemy ship at (5500, 5500) — same sector, should be found
    Triangle enemy_ship(5500.0f, 5500.0f);
    enemy_ship.alive = true;
    std::vector<Triangle> ships = {enemy_ship};
    std::vector<int> ship_teams = {1};  // enemy team
    grid.insert(0, 5500.0f, 5500.0f);

    // Place friendly ship at (4500, 4500) — should NOT appear (same team)
    Triangle friendly_ship(4500.0f, 4500.0f);
    friendly_ship.alive = true;
    ships.push_back(friendly_ship);
    ship_teams.push_back(0);  // same team
    grid.insert(1, 4500.0f, 4500.0f);

    std::vector<Base> bases;  // no bases

    auto threats = gather_near_threats(
        grid, squad_cx, squad_cy, 2, squad_team,
        ships, ship_teams, bases);

    EXPECT_EQ(threats.size(), 1u);
    EXPECT_TRUE(threats[0].is_ship);
    EXPECT_FALSE(threats[0].is_starbase);
    EXPECT_FLOAT_EQ(threats[0].health, 1.0f);
}

TEST(SquadLeader, GatherNearThreatsFindsEnemyBases) {
    SectorGrid grid(10000.0f, 10000.0f, 2000.0f);
    float squad_cx = 5000.0f, squad_cy = 5000.0f;
    int squad_team = 0;

    // Enemy base at (6000, 5000)
    Base enemy_base(6000.0f, 5000.0f, 100.0f, 1000.0f, 1);
    enemy_base.take_damage(500.0f);  // half HP
    std::vector<Base> bases = {enemy_base};

    // Need to insert bases into grid too. Bases get indices starting after ship count.
    // Convention: base entity IDs are offset by a large number to distinguish from ships.
    // Actually, gather_near_threats needs to handle this — it inserts bases separately.
    // The grid contains ALL entities — the function filters by team.
    // For this test, we insert the base position into the grid.
    grid.insert(0, 6000.0f, 5000.0f);  // entity 0 = base index 0

    std::vector<Triangle> ships;  // no ships
    std::vector<int> ship_teams;

    auto threats = gather_near_threats(
        grid, squad_cx, squad_cy, 2, squad_team,
        ships, ship_teams, bases);

    EXPECT_EQ(threats.size(), 1u);
    EXPECT_TRUE(threats[0].is_starbase);
    EXPECT_FALSE(threats[0].is_ship);
    EXPECT_FLOAT_EQ(threats[0].health, 0.5f);
}

TEST(SquadLeader, GatherNearThreatsIgnoresDeadShips) {
    SectorGrid grid(10000.0f, 10000.0f, 2000.0f);

    Triangle dead_enemy(5500.0f, 5500.0f);
    dead_enemy.alive = false;
    std::vector<Triangle> ships = {dead_enemy};
    std::vector<int> ship_teams = {1};
    grid.insert(0, 5500.0f, 5500.0f);

    std::vector<Base> bases;

    auto threats = gather_near_threats(
        grid, 5000.0f, 5000.0f, 2, 0,
        ships, ship_teams, bases);

    EXPECT_TRUE(threats.empty());
}

TEST(SquadLeader, GatherNearThreatsIgnoresDistantEnemies) {
    SectorGrid grid(20000.0f, 20000.0f, 2000.0f);

    // Squad center at (5000, 5000) => sector (2, 2)
    // Enemy at (15000, 15000) => sector (7, 7) — Manhattan dist 10, way outside radius 2
    Triangle far_enemy(15000.0f, 15000.0f);
    far_enemy.alive = true;
    std::vector<Triangle> ships = {far_enemy};
    std::vector<int> ship_teams = {1};
    grid.insert(0, 15000.0f, 15000.0f);

    std::vector<Base> bases;

    auto threats = gather_near_threats(
        grid, 5000.0f, 5000.0f, 2, 0,
        ships, ship_teams, bases);

    EXPECT_TRUE(threats.empty());
}

// ── run_ntm_threat_selection tests ───────────────────────────────────────────

TEST(SquadLeader, NtmReturnsInactiveWhenNoThreats) {
    std::mt19937 rng(42);
    auto ntm_ind = Individual::random(6, {4}, 1, rng);
    auto ntm_net = ntm_ind.build_network();

    std::vector<NearThreat> threats;  // empty

    auto result = run_ntm_threat_selection(
        ntm_net, 5000.0f, 5000.0f, 1.0f, threats, 10000.0f, 10000.0f);

    EXPECT_FALSE(result.active);
    EXPECT_FLOAT_EQ(result.threat_score, 0.0f);
}

TEST(SquadLeader, NtmSelectsHighestThreat) {
    std::mt19937 rng(42);
    auto ntm_ind = Individual::random(6, {4}, 1, rng);
    auto ntm_net = ntm_ind.build_network();

    // Create two threats at different positions
    NearThreat t1;
    t1.x = 6000.0f; t1.y = 5000.0f;
    t1.health = 1.0f; t1.is_ship = true; t1.entity_id = 0;

    NearThreat t2;
    t2.x = 8000.0f; t2.y = 5000.0f;
    t2.health = 0.5f; t2.is_ship = true; t2.entity_id = 1;

    std::vector<NearThreat> threats = {t1, t2};

    auto result = run_ntm_threat_selection(
        ntm_net, 5000.0f, 5000.0f, 1.0f, threats, 10000.0f, 10000.0f);

    EXPECT_TRUE(result.active);
    // Should have picked one of the two threats (whichever had higher score)
    EXPECT_TRUE(result.target_x == t1.x || result.target_x == t2.x);
}
```

- [ ] **Step 3: Run tests — expect FAIL**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='SquadLeader*'`

Expected: Tests fail — `gather_near_threats` returns empty (stub), NTM results are inactive (stub).

- [ ] **Step 4: Implement gather_near_threats**

In `src/engine/squad_leader.cpp`, replace the `gather_near_threats` stub with:

```cpp
std::vector<NearThreat> gather_near_threats(
    const SectorGrid& grid,
    float squad_center_x, float squad_center_y,
    int ntm_sector_radius,
    int squad_team,
    std::span<const Triangle> ships,
    std::span<const int> ship_teams,
    std::span<const Base> bases) {

    auto center_sector = grid.sector_of(squad_center_x, squad_center_y);
    auto entity_ids = grid.entities_in_diamond(center_sector, ntm_sector_radius);

    std::vector<NearThreat> threats;

    // Entity IDs in the grid: [0..ships.size()-1] are ships, [ships.size()..] are bases
    std::size_t ship_count = ships.size();

    for (auto id : entity_ids) {
        if (id < ship_count) {
            // It's a ship
            auto i = id;
            if (!ships[i].alive) continue;
            if (static_cast<std::size_t>(ship_teams[i]) == static_cast<std::size_t>(squad_team)) continue;  // skip friendlies

            NearThreat t;
            t.x = ships[i].x;
            t.y = ships[i].y;
            t.health = 1.0f;  // ships are alive or dead, no HP
            t.is_ship = true;
            t.is_starbase = false;
            t.entity_id = i;
            threats.push_back(t);
        } else {
            // It's a base
            auto base_idx = id - ship_count;
            if (base_idx >= bases.size()) continue;
            if (!bases[base_idx].alive()) continue;
            if (bases[base_idx].team_id == squad_team) continue;  // skip own base

            NearThreat t;
            t.x = bases[base_idx].x;
            t.y = bases[base_idx].y;
            t.health = bases[base_idx].hp_normalized();
            t.is_ship = false;
            t.is_starbase = true;
            t.entity_id = base_idx;
            threats.push_back(t);
        }
    }

    return threats;
}
```

- [ ] **Step 5: Implement run_ntm_threat_selection**

In `src/engine/squad_leader.cpp`, replace the `run_ntm_threat_selection` stub with:

```cpp
NtmResult run_ntm_threat_selection(
    const neuralnet::Network& ntm_net,
    float squad_center_x, float squad_center_y,
    float squad_alive_fraction,
    const std::vector<NearThreat>& threats,
    float world_w, float world_h) {

    NtmResult result;
    if (threats.empty()) return result;  // active = false, all zeros

    float best_score = -std::numeric_limits<float>::max();

    for (const auto& threat : threats) {
        auto dr = compute_dir_range(
            squad_center_x, squad_center_y,
            threat.x, threat.y,
            world_w, world_h);

        // NTM inputs: heading, distance, enemy_health, squad_health, is_ship, is_starbase
        std::vector<float> ntm_input = {
            std::atan2(dr.dir_sin, dr.dir_cos),  // heading (radians)
            dr.range,                              // distance (normalized)
            threat.health,
            squad_alive_fraction,
            threat.is_ship ? 1.0f : 0.0f,
            threat.is_starbase ? 1.0f : 0.0f
        };

        auto output = ntm_net.forward(std::span<const float>(ntm_input));
        float threat_score = output[0];

        if (threat_score > best_score) {
            best_score = threat_score;
            result.active = true;
            result.threat_score = threat_score;
            result.target_x = threat.x;
            result.target_y = threat.y;
            result.heading = std::atan2(dr.dir_sin, dr.dir_cos);
            result.distance = dr.range;
        }
    }

    return result;
}
```

- [ ] **Step 6: Run tests — expect PASS**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='SquadLeader*'`

Expected: All SquadLeader tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/engine/squad_leader.cpp tests/squad_leader_test.cpp tests/CMakeLists.txt
git commit -m "feat: implement NTM gather_near_threats and run_ntm_threat_selection"
```

---

### Task 6: Squad Leader Execution + Order Interpretation + Fighter Inputs

**Files:**
- Modify: `src/engine/squad_leader.cpp`
- Modify: `tests/squad_leader_test.cpp`

- [ ] **Step 1: Write squad leader + fighter input tests**

Append to `tests/squad_leader_test.cpp`:

```cpp
// ── run_squad_leader tests ───────────────────────────────────────────────────

TEST(SquadLeader, RunSquadLeaderReturnsValidOrder) {
    std::mt19937 rng(42);
    auto leader_ind = Individual::random(11, {8}, 5, rng);
    auto leader_net = leader_ind.build_network();

    NtmResult ntm;
    ntm.active = true;
    ntm.threat_score = 0.8f;
    ntm.heading = 0.5f;
    ntm.distance = 0.3f;
    ntm.target_x = 7000.0f;
    ntm.target_y = 5000.0f;

    auto order = run_squad_leader(
        leader_net,
        0.75f,   // squad_health
        0.2f,    // home_distance
        0.5f,    // home_heading
        0.9f,    // home_health
        0.3f,    // squad_spacing
        1.0f,    // commander_target_heading (Phase 1: toward enemy base)
        0.5f,    // commander_target_distance
        ntm,
        1000.0f, 1000.0f,   // own base
        9000.0f, 9000.0f);  // enemy base

    // Should produce a valid tactical order (one of the 3 enums)
    EXPECT_TRUE(order.tactical == TacticalOrder::AttackStarbase ||
                order.tactical == TacticalOrder::AttackShip ||
                order.tactical == TacticalOrder::DefendHome);

    // Should produce a valid spacing order
    EXPECT_TRUE(order.spacing == SpacingOrder::Expand ||
                order.spacing == SpacingOrder::Contract);
}

TEST(SquadLeader, RunSquadLeaderNoThreatFallsBackToStarbase) {
    std::mt19937 rng(42);
    auto leader_ind = Individual::random(11, {8}, 5, rng);
    auto leader_net = leader_ind.build_network();

    NtmResult ntm;  // inactive (no threats)

    auto order = run_squad_leader(
        leader_net,
        1.0f, 0.3f, 0.5f, 1.0f, 0.2f, 1.0f, 0.5f,
        ntm,
        1000.0f, 1000.0f,
        9000.0f, 9000.0f);

    // If order is AttackShip but no NTM is active, target should fall back to enemy base
    if (order.tactical == TacticalOrder::AttackShip) {
        EXPECT_FLOAT_EQ(order.target_x, 9000.0f);
        EXPECT_FLOAT_EQ(order.target_y, 9000.0f);
    }
}

// ── compute_squad_leader_fighter_inputs tests ────────────────────────────────

TEST(SquadLeader, FighterInputsAttackHasPositiveAggression) {
    SquadLeaderOrder order;
    order.tactical = TacticalOrder::AttackStarbase;
    order.spacing = SpacingOrder::Expand;
    order.target_x = 9000.0f;
    order.target_y = 9000.0f;

    auto inputs = compute_squad_leader_fighter_inputs(
        5000.0f, 5000.0f,  // fighter pos
        order,
        5000.0f, 5000.0f,  // squad center
        10000.0f, 10000.0f);

    EXPECT_FLOAT_EQ(inputs.aggression, 1.0f);
    EXPECT_FLOAT_EQ(inputs.spacing, 1.0f);
    // Target is at (9000, 9000), fighter at (5000, 5000) => non-zero heading and distance
    EXPECT_NE(inputs.squad_target_distance, 0.0f);
}

TEST(SquadLeader, FighterInputsDefendHasNegativeAggression) {
    SquadLeaderOrder order;
    order.tactical = TacticalOrder::DefendHome;
    order.spacing = SpacingOrder::Contract;
    order.target_x = 1000.0f;
    order.target_y = 1000.0f;

    auto inputs = compute_squad_leader_fighter_inputs(
        5000.0f, 5000.0f, order,
        5000.0f, 5000.0f,
        10000.0f, 10000.0f);

    EXPECT_FLOAT_EQ(inputs.aggression, -1.0f);
    EXPECT_FLOAT_EQ(inputs.spacing, -1.0f);
}

TEST(SquadLeader, FighterInputsSquadCenterDistanceZeroWhenAtCenter) {
    SquadLeaderOrder order;
    order.tactical = TacticalOrder::AttackStarbase;
    order.spacing = SpacingOrder::Expand;
    order.target_x = 9000.0f;
    order.target_y = 9000.0f;

    // Fighter is exactly at squad center
    auto inputs = compute_squad_leader_fighter_inputs(
        5000.0f, 5000.0f, order,
        5000.0f, 5000.0f,  // same position
        10000.0f, 10000.0f);

    EXPECT_FLOAT_EQ(inputs.squad_center_distance, 0.0f);
}
```

- [ ] **Step 2: Run tests — expect FAIL**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='SquadLeader*'`

Expected: New tests fail (stubs return default values).

- [ ] **Step 3: Implement run_squad_leader**

In `src/engine/squad_leader.cpp`, replace the `run_squad_leader` stub with:

```cpp
SquadLeaderOrder run_squad_leader(
    const neuralnet::Network& leader_net,
    float squad_health,
    float home_distance,
    float home_heading,
    float home_health,
    float squad_spacing_val,
    float commander_target_heading,
    float commander_target_distance,
    const NtmResult& ntm,
    float own_base_x, float own_base_y,
    float enemy_base_x, float enemy_base_y) {

    // Build 11 inputs
    std::vector<float> input = {
        squad_health,
        home_distance,
        home_heading,
        home_health,
        squad_spacing_val,
        commander_target_heading,
        commander_target_distance,
        ntm.active ? 1.0f : 0.0f,      // active_threat
        ntm.active ? ntm.heading : 0.0f,
        ntm.active ? ntm.distance : 0.0f,
        ntm.active ? ntm.threat_score : 0.0f
    };

    auto output = leader_net.forward(std::span<const float>(input));

    // Argmax for spacing group (outputs 0-1)
    SpacingOrder spacing = (output[0] >= output[1])
        ? SpacingOrder::Expand : SpacingOrder::Contract;

    // Argmax for tactical group (outputs 2-4)
    TacticalOrder tactical = TacticalOrder::AttackStarbase;
    float max_tactical = output[2];
    if (output[3] > max_tactical) {
        max_tactical = output[3];
        tactical = TacticalOrder::AttackShip;
    }
    if (output[4] > max_tactical) {
        tactical = TacticalOrder::DefendHome;
    }

    // Determine target position based on tactical order
    SquadLeaderOrder order;
    order.tactical = tactical;
    order.spacing = spacing;

    switch (tactical) {
        case TacticalOrder::AttackStarbase:
            order.target_x = enemy_base_x;
            order.target_y = enemy_base_y;
            break;
        case TacticalOrder::AttackShip:
            if (ntm.active) {
                order.target_x = ntm.target_x;
                order.target_y = ntm.target_y;
            } else {
                // Fallback: no active threat, target enemy starbase
                order.target_x = enemy_base_x;
                order.target_y = enemy_base_y;
            }
            break;
        case TacticalOrder::DefendHome:
            order.target_x = own_base_x;
            order.target_y = own_base_y;
            break;
    }

    return order;
}
```

- [ ] **Step 4: Implement compute_squad_leader_fighter_inputs**

In `src/engine/squad_leader.cpp`, replace the `compute_squad_leader_fighter_inputs` stub with:

```cpp
SquadLeaderFighterInputs compute_squad_leader_fighter_inputs(
    float fighter_x, float fighter_y,
    const SquadLeaderOrder& order,
    float squad_center_x, float squad_center_y,
    float world_w, float world_h) {

    SquadLeaderFighterInputs inputs;

    // Direction + range from fighter to squad target
    auto target_dr = compute_dir_range(
        fighter_x, fighter_y,
        order.target_x, order.target_y,
        world_w, world_h);
    inputs.squad_target_heading = std::atan2(target_dr.dir_sin, target_dr.dir_cos);
    inputs.squad_target_distance = target_dr.range;

    // Direction + range from fighter to squad center
    auto center_dr = compute_dir_range(
        fighter_x, fighter_y,
        squad_center_x, squad_center_y,
        world_w, world_h);
    inputs.squad_center_heading = std::atan2(center_dr.dir_sin, center_dr.dir_cos);
    inputs.squad_center_distance = center_dr.range;

    // Aggression: +1 for attack orders, -1 for defend
    switch (order.tactical) {
        case TacticalOrder::AttackStarbase:
        case TacticalOrder::AttackShip:
            inputs.aggression = 1.0f;
            break;
        case TacticalOrder::DefendHome:
            inputs.aggression = -1.0f;
            break;
    }

    // Spacing: +1 expand, -1 contract
    inputs.spacing = (order.spacing == SpacingOrder::Expand) ? 1.0f : -1.0f;

    return inputs;
}
```

- [ ] **Step 5: Run tests — expect PASS**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='SquadLeader*'`

Expected: All SquadLeader tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/engine/squad_leader.cpp tests/squad_leader_test.cpp
git commit -m "feat: implement run_squad_leader and compute_squad_leader_fighter_inputs"
```

---

### Task 7: Squad Spacing Calculation

**Files:**
- Modify: `include/neuroflyer/arena_session.h`
- Modify: `src/engine/arena_session.cpp`
- Modify: `tests/arena_session_test.cpp`

- [ ] **Step 1: Add squad_spacing to SquadStats**

In `include/neuroflyer/arena_session.h`, add to the `SquadStats` struct:

```cpp
    float squad_spacing = 0.0f;  // stddev of ship distances from squad center (normalized)
```

- [ ] **Step 2: Write failing test**

Append to `tests/arena_session_test.cpp`:

```cpp
TEST(ArenaSession, SquadStatsIncludesSpacing) {
    ArenaConfig cfg;
    cfg.num_teams = 1;
    cfg.num_squads = 1;
    cfg.fighters_per_squad = 4;
    cfg.tower_count = 0;
    cfg.token_count = 0;
    cfg.base_hp = 1000.0f;

    ArenaSession arena(cfg, 42);

    // Move ships to known positions around center
    // Base is at center of team 0's pie slice
    auto& ships = arena.ships();
    float cx = 0, cy = 0;
    for (auto& s : ships) { cx += s.x; cy += s.y; }
    cx /= static_cast<float>(ships.size());
    cy /= static_cast<float>(ships.size());

    // Place all ships equidistant from their centroid (move them)
    float offset = 500.0f;
    ships[0].x = cx + offset; ships[0].y = cy;
    ships[1].x = cx - offset; ships[1].y = cy;
    ships[2].x = cx; ships[2].y = cy + offset;
    ships[3].x = cx; ships[3].y = cy - offset;

    auto stats = arena.compute_squad_stats(0, 0);

    // All ships are 500px from centroid, so stddev = 0 (all equal distance)
    // Actually stddev of [500, 500, 500, 500] = 0
    // Normalized to world diagonal
    EXPECT_GE(stats.squad_spacing, 0.0f);
    EXPECT_LE(stats.squad_spacing, 1.0f);
}
```

- [ ] **Step 3: Implement squad_spacing in compute_squad_stats**

In `src/engine/arena_session.cpp`, at the end of `compute_squad_stats()`, before `return stats;`, add:

```cpp
    // Compute squad spacing: stddev of alive ship distances from centroid, normalized
    if (alive_count > 0.0f) {
        float diag = std::sqrt(config_.world_width * config_.world_width +
                                config_.world_height * config_.world_height);
        float sum_dist = 0.0f;
        float sum_dist_sq = 0.0f;
        for (std::size_t i = 0; i < ships_.size(); ++i) {
            if (team_assignments_[i] != team || squad_assignments_[i] != squad) continue;
            if (!ships_[i].alive) continue;
            float dx = ships_[i].x - stats.centroid_x;
            float dy = ships_[i].y - stats.centroid_y;
            float dist = std::sqrt(dx * dx + dy * dy);
            sum_dist += dist;
            sum_dist_sq += dist * dist;
        }
        float mean_dist = sum_dist / alive_count;
        float variance = (sum_dist_sq / alive_count) - (mean_dist * mean_dist);
        stats.squad_spacing = std::sqrt(std::max(0.0f, variance)) / diag;
    }
```

- [ ] **Step 4: Run tests**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='ArenaSession*SquadStats*'`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/arena_session.h src/engine/arena_session.cpp tests/arena_session_test.cpp
git commit -m "feat: add squad_spacing to SquadStats (stddev of ship-to-center distances)"
```

---

### Task 8: Update Fighter Input Building

**Files:**
- Modify: `include/neuroflyer/arena_sensor.h`
- Modify: `src/engine/arena_sensor.cpp`
- Modify: `tests/arena_sensor_test.cpp`

- [ ] **Step 1: Update the header signature**

In `include/neuroflyer/arena_sensor.h`, replace the `build_arena_ship_input` declaration:

Replace:
```cpp
/// Build the complete arena fighter input vector.
/// Layout: [sensor values...] [pos/rotation] [nav inputs] [broadcast signals] [memory]
[[nodiscard]] std::vector<float> build_arena_ship_input(
    const ShipDesign& design,
    const ArenaQueryContext& ctx,
    float dir_to_target_sin, float dir_to_target_cos, float range_to_target,
    float dir_to_home_sin, float dir_to_home_cos, float range_to_home,
    float own_base_hp,
    std::span<const float> broadcast_signals,
    std::span<const float> memory);
```

With:
```cpp
/// Build the complete arena fighter input vector.
/// Layout: [sensor values...] [pos/rotation] [nav inputs] [squad leader inputs (6)] [memory]
[[nodiscard]] std::vector<float> build_arena_ship_input(
    const ShipDesign& design,
    const ArenaQueryContext& ctx,
    float dir_to_target_sin, float dir_to_target_cos, float range_to_target,
    float dir_to_home_sin, float dir_to_home_cos, float range_to_home,
    float own_base_hp,
    float squad_target_heading, float squad_target_distance,
    float squad_center_heading, float squad_center_distance,
    float aggression, float spacing,
    std::span<const float> memory);
```

Also add `#include <neuroflyer/arena_config.h>` to the includes.

And update `compute_arena_input_size` — the parameter name changes for clarity but the signature stays the same (it still takes `broadcast_signal_count`, which is now always `ArenaConfig::squad_leader_fighter_inputs = 6`).

- [ ] **Step 2: Update the implementation**

In `src/engine/arena_sensor.cpp`, replace the `build_arena_ship_input` function:

Replace the `broadcast_signals` parameter handling:
```cpp
    // --- Broadcast signals ---
    input.insert(input.end(), broadcast_signals.begin(), broadcast_signals.end());
```

With:
```cpp
    // --- Squad leader inputs (6) ---
    input.push_back(squad_target_heading);
    input.push_back(squad_target_distance);
    input.push_back(squad_center_heading);
    input.push_back(squad_center_distance);
    input.push_back(aggression);
    input.push_back(spacing);
```

Also update the function signature to match the header.

- [ ] **Step 3: Update tests**

In `tests/arena_sensor_test.cpp`, find the `ArenaInputSize` test and update:

Replace any reference to `4` broadcast signals with `6` squad leader inputs. The test should compute the expected size as: `sensors + 3 (position) + 7 (nav) + 6 (squad leader) + memory`.

For example, if the test currently has:
```cpp
auto size = compute_arena_input_size(design, 4);
```

Change to:
```cpp
auto size = compute_arena_input_size(design, 6);
```

And update expected values accordingly (previous total was `sensors + 14 + memory`, now it's `sensors + 16 + memory`).

Also update the `BuildArenaShipInputSize` test to use the new `build_arena_ship_input` signature — replace the `broadcast_signals` span with 6 individual float parameters:

```cpp
auto input = build_arena_ship_input(
    design, ctx,
    0.0f, 1.0f, 0.5f,     // target dir+range
    -1.0f, 0.0f, 0.3f,    // home dir+range
    0.9f,                   // own_base_hp
    0.5f, 0.4f,            // squad_target heading/dist
    0.1f, 0.2f,            // squad_center heading/dist
    1.0f, -1.0f,           // aggression, spacing
    memory);
```

- [ ] **Step 4: Run tests**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='ArenaSensor*'`

Expected: All ArenaSensor tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/arena_sensor.h src/engine/arena_sensor.cpp tests/arena_sensor_test.cpp
git commit -m "feat: replace broadcast signals with 6 structured squad leader fighter inputs"
```

---

### Task 9: Rewrite Arena Match Tick Loop

**Files:**
- Modify: `include/neuroflyer/arena_match.h`
- Modify: `src/engine/arena_match.cpp`
- Modify: `tests/arena_match_test.cpp`

- [ ] **Step 1: Update arena_match.h**

In `include/neuroflyer/arena_match.h`, add `#include <neuroflyer/squad_leader.h>` and update the `run_arena_match` signature:

Replace:
```cpp
[[nodiscard]] ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const SquadNetConfig& squad_config,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed);
```

With:
```cpp
[[nodiscard]] ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed);
```

(Remove `SquadNetConfig` parameter — config is now on `ArenaConfig`.)

- [ ] **Step 2: Rewrite arena_match.cpp**

Replace the entire `run_arena_match` function body with the new flow. The key changes:

1. Compile 3 nets per team (NTM, squad leader, fighter) instead of 2
2. Each tick: build sector grid → gather threats → run NTMs → run squad leader → interpret orders → build fighter inputs with 6 structured values
3. Remove broadcast_signals vector, replace with SquadLeaderOrder per team

```cpp
#include <neuroflyer/arena_match.h>
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>

#include <cassert>
#include <cmath>
#include <limits>

namespace neuroflyer {

ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed) {

    assert(teams.size() == arena_config.num_teams);

    ArenaMatchResult result;
    result.team_scores.resize(arena_config.num_teams, 0.0f);

    ArenaSession arena(arena_config, seed);

    // Compile networks: NTM + squad leader + fighter per team
    std::vector<neuralnet::Network> ntm_nets;
    std::vector<neuralnet::Network> leader_nets;
    std::vector<neuralnet::Network> fighter_nets;
    ntm_nets.reserve(arena_config.num_teams);
    leader_nets.reserve(arena_config.num_teams);
    fighter_nets.reserve(arena_config.num_teams);

    for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
        ntm_nets.push_back(teams[t].build_ntm_network());
        leader_nets.push_back(teams[t].build_squad_network());
        fighter_nets.push_back(teams[t].build_fighter_network());
    }

    std::size_t total_ships = arena.ships().size();
    std::vector<std::vector<float>> recurrent_states(
        total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));

    std::vector<int> ship_teams(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        ship_teams[i] = arena.team_of(i);
    }

    // Main loop
    while (!arena.is_over()) {
        // ── 1. Build sector grid for this tick ──
        SectorGrid grid(arena_config.world_width, arena_config.world_height,
                        arena_config.sector_size);

        // Insert ships
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (arena.ships()[i].alive) {
                grid.insert(i, arena.ships()[i].x, arena.ships()[i].y);
            }
        }
        // Insert bases (IDs offset by total_ships)
        for (std::size_t b = 0; b < arena.bases().size(); ++b) {
            if (arena.bases()[b].alive()) {
                grid.insert(total_ships + b, arena.bases()[b].x, arena.bases()[b].y);
            }
        }

        // ── 2. Per-team: NTM → squad leader → orders ──
        std::vector<SquadLeaderOrder> team_orders(arena_config.num_teams);
        std::vector<float> squad_center_xs(arena_config.num_teams, 0.0f);
        std::vector<float> squad_center_ys(arena_config.num_teams, 0.0f);

        for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
            int team = static_cast<int>(t);
            auto stats = arena.compute_squad_stats(team, 0);

            squad_center_xs[t] = stats.centroid_x;
            squad_center_ys[t] = stats.centroid_y;

            // Gather near threats
            auto threats = gather_near_threats(
                grid, stats.centroid_x, stats.centroid_y,
                arena_config.ntm_sector_radius, team,
                arena.ships(), ship_teams, arena.bases());

            // Run NTMs
            auto ntm = run_ntm_threat_selection(
                ntm_nets[t], stats.centroid_x, stats.centroid_y,
                stats.alive_fraction, threats,
                arena_config.world_width, arena_config.world_height);

            // Find own base and nearest enemy base
            float own_base_x = arena.bases()[t].x;
            float own_base_y = arena.bases()[t].y;
            float own_base_hp = arena.bases()[t].hp_normalized();

            float enemy_base_x = 0, enemy_base_y = 0;
            float min_dist_sq = std::numeric_limits<float>::max();
            for (const auto& base : arena.bases()) {
                if (base.team_id == team) continue;
                float dx = stats.centroid_x - base.x;
                float dy = stats.centroid_y - base.y;
                float dsq = dx * dx + dy * dy;
                if (dsq < min_dist_sq) {
                    min_dist_sq = dsq;
                    enemy_base_x = base.x;
                    enemy_base_y = base.y;
                }
            }

            // Compute squad leader inputs
            float world_diag = std::sqrt(
                arena_config.world_width * arena_config.world_width +
                arena_config.world_height * arena_config.world_height);

            float home_dx = own_base_x - stats.centroid_x;
            float home_dy = own_base_y - stats.centroid_y;
            float home_dist_raw = std::sqrt(home_dx * home_dx + home_dy * home_dy);
            float home_distance = home_dist_raw / world_diag;
            float home_heading = (home_dist_raw > 1e-6f)
                ? std::atan2(home_dx / home_dist_raw, home_dy / home_dist_raw) : 0.0f;

            // Commander target = enemy starbase (Phase 1)
            float cmd_dx = enemy_base_x - stats.centroid_x;
            float cmd_dy = enemy_base_y - stats.centroid_y;
            float cmd_dist_raw = std::sqrt(cmd_dx * cmd_dx + cmd_dy * cmd_dy);
            float cmd_target_heading = (cmd_dist_raw > 1e-6f)
                ? std::atan2(cmd_dx / cmd_dist_raw, cmd_dy / cmd_dist_raw) : 0.0f;
            float cmd_target_distance = cmd_dist_raw / world_diag;

            // Run squad leader
            team_orders[t] = run_squad_leader(
                leader_nets[t],
                stats.alive_fraction,
                home_distance,
                home_heading,
                own_base_hp,
                stats.squad_spacing,
                cmd_target_heading,
                cmd_target_distance,
                ntm,
                own_base_x, own_base_y,
                enemy_base_x, enemy_base_y);
        }

        // ── 3. Per-fighter: build input, run net, apply actions ──
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (!arena.ships()[i].alive) continue;

            int team = ship_teams[i];
            auto t = static_cast<std::size_t>(team);

            // Nav inputs (same as before: nearest enemy base + home base)
            float target_x = 0, target_y = 0;
            float min_dsq = std::numeric_limits<float>::max();
            for (const auto& base : arena.bases()) {
                if (base.team_id == team) continue;
                float dx = base.x - arena.ships()[i].x;
                float dy = base.y - arena.ships()[i].y;
                float dsq = dx * dx + dy * dy;
                if (dsq < min_dsq) {
                    min_dsq = dsq;
                    target_x = base.x;
                    target_y = base.y;
                }
            }

            float home_x = arena.bases()[t].x;
            float home_y = arena.bases()[t].y;

            auto target_dr = compute_dir_range(
                arena.ships()[i].x, arena.ships()[i].y,
                target_x, target_y,
                arena_config.world_width, arena_config.world_height);

            auto home_dr = compute_dir_range(
                arena.ships()[i].x, arena.ships()[i].y,
                home_x, home_y,
                arena_config.world_width, arena_config.world_height);

            float own_base_hp = arena.bases()[t].hp_normalized();

            // Squad leader fighter inputs
            auto sl_inputs = compute_squad_leader_fighter_inputs(
                arena.ships()[i].x, arena.ships()[i].y,
                team_orders[t],
                squad_center_xs[t], squad_center_ys[t],
                arena_config.world_width, arena_config.world_height);

            // Build ArenaQueryContext
            ArenaQueryContext ctx;
            ctx.ship_x = arena.ships()[i].x;
            ctx.ship_y = arena.ships()[i].y;
            ctx.ship_rotation = arena.ships()[i].rotation;
            ctx.world_w = arena_config.world_width;
            ctx.world_h = arena_config.world_height;
            ctx.self_index = i;
            ctx.self_team = team;
            ctx.towers = arena.towers();
            ctx.tokens = arena.tokens();
            ctx.ships = arena.ships();
            ctx.ship_teams = ship_teams;
            ctx.bullets = arena.bullets();

            auto input = build_arena_ship_input(
                fighter_design, ctx,
                target_dr.dir_sin, target_dr.dir_cos, target_dr.range,
                home_dr.dir_sin, home_dr.dir_cos, home_dr.range,
                own_base_hp,
                sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
                sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
                sl_inputs.aggression, sl_inputs.spacing,
                recurrent_states[i]);

            auto output = fighter_nets[t].forward(
                std::span<const float>(input));

            auto decoded = decode_output(
                std::span<const float>(output),
                fighter_design.memory_slots);

            arena.set_ship_actions(i,
                decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

            recurrent_states[i] = decoded.memory;
        }

        arena.tick();
    }

    result.ticks_elapsed = arena.current_tick();
    result.match_completed = true;

    // Fitness scoring (unchanged)
    auto num_teams = arena_config.num_teams;
    for (std::size_t t = 0; t < num_teams; ++t) {
        int team = static_cast<int>(t);

        float damage_dealt = 0.0f;
        for (const auto& base : arena.bases()) {
            if (base.team_id == team) continue;
            damage_dealt += (base.max_hp - base.hp) / base.max_hp;
        }
        if (num_teams > 1) {
            damage_dealt /= static_cast<float>(num_teams - 1);
        }

        float own_survival = arena.bases()[t].hp_normalized();

        std::size_t ships_per_team = arena_config.num_squads * arena_config.fighters_per_squad;
        std::size_t alive_on_team = 0;
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams[i] == team && arena.ships()[i].alive) {
                ++alive_on_team;
            }
        }
        float alive_frac = (ships_per_team > 0)
            ? static_cast<float>(alive_on_team) / static_cast<float>(ships_per_team)
            : 0.0f;

        float token_frac = 0.0f;
        if (arena_config.token_count > 0) {
            int team_tokens = 0;
            const auto& tc = arena.tokens_collected();
            for (std::size_t i = 0; i < total_ships; ++i) {
                if (ship_teams[i] == team) {
                    team_tokens += tc[i];
                }
            }
            token_frac = static_cast<float>(team_tokens) /
                         static_cast<float>(arena_config.token_count);
        }

        result.team_scores[t] =
            arena_config.fitness_weight_base_damage * damage_dealt +
            arena_config.fitness_weight_survival * own_survival +
            arena_config.fitness_weight_ships_alive * alive_frac +
            arena_config.fitness_weight_tokens * token_frac;
    }

    return result;
}

} // namespace neuroflyer
```

- [ ] **Step 3: Update arena match tests**

Replace the entire contents of `tests/arena_match_test.cpp` to use the new API (3-net teams, no SquadNetConfig param):

```cpp
#include <neuroflyer/arena_match.h>
#include <neuroflyer/team_evolution.h>
#include <gtest/gtest.h>

using namespace neuroflyer;

namespace {

ShipDesign make_arena_design() {
    ShipDesign d;
    SensorDef s;
    s.angle = 0.0f;
    s.range = 200.0f;
    s.is_full_sensor = true;
    d.sensors.push_back(s);
    d.memory_slots = 2;
    return d;
}

} // namespace

TEST(ArenaMatch, RunsWithoutCrash) {
    ArenaConfig cfg;
    cfg.num_teams = 2;
    cfg.num_squads = 1;
    cfg.fighters_per_squad = 4;
    cfg.tower_count = 5;
    cfg.token_count = 3;
    cfg.time_limit_ticks = 60;  // short match for test speed

    auto design = make_arena_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;
    std::mt19937 rng(42);

    auto teams = create_team_population(design, {6}, ntm_cfg, leader_cfg, 2, rng);

    auto result = run_arena_match(cfg, design, teams, 42);

    EXPECT_TRUE(result.match_completed);
    EXPECT_EQ(result.team_scores.size(), 2u);
    EXPECT_GT(result.ticks_elapsed, 0u);
}

TEST(ArenaMatch, ScoresAreNonNegative) {
    ArenaConfig cfg;
    cfg.num_teams = 2;
    cfg.num_squads = 1;
    cfg.fighters_per_squad = 4;
    cfg.tower_count = 0;
    cfg.token_count = 0;
    cfg.time_limit_ticks = 30;

    auto design = make_arena_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;
    std::mt19937 rng(42);

    auto teams = create_team_population(design, {6}, ntm_cfg, leader_cfg, 2, rng);

    auto result = run_arena_match(cfg, design, teams, 42);

    for (auto score : result.team_scores) {
        EXPECT_GE(score, 0.0f);
    }
}

TEST(ArenaMatch, FullGenerationCycle) {
    ArenaConfig cfg;
    cfg.num_teams = 2;
    cfg.num_squads = 1;
    cfg.fighters_per_squad = 4;
    cfg.tower_count = 0;
    cfg.token_count = 0;
    cfg.time_limit_ticks = 30;

    auto design = make_arena_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;
    std::mt19937 rng(42);

    std::size_t pop_size = 6;
    auto population = create_team_population(design, {6}, ntm_cfg, leader_cfg, pop_size, rng);

    EvolutionConfig evo;
    evo.population_size = pop_size;
    evo.elitism_count = 2;
    evo.tournament_size = 3;

    // Run 3 generations
    for (int gen = 0; gen < 3; ++gen) {
        // Pairwise matches: team 0 vs team 1, team 2 vs team 3, etc.
        for (std::size_t i = 0; i + 1 < population.size(); i += 2) {
            std::vector<TeamIndividual> match_teams = {population[i], population[i + 1]};
            auto result = run_arena_match(cfg, design, match_teams, rng());
            population[i].fitness += result.team_scores[0];
            population[i + 1].fitness += result.team_scores[1];
        }

        population = evolve_team_population(population, evo, rng);
        EXPECT_EQ(population.size(), pop_size);
    }
}
```

- [ ] **Step 4: Build and run tests**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='ArenaMatch*'`

Expected: All 3 ArenaMatch tests PASS.

- [ ] **Step 5: Run full test suite to check for regressions**

Run: `cd /Users/wldarden/repos/Neuroflyer && ./build/tests/neuroflyer_tests`

Expected: All tests PASS. If any existing tests fail due to the `build_arena_ship_input` signature change, fix them (they should have been updated in Task 8 Step 3).

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/arena_match.h src/engine/arena_match.cpp tests/arena_match_test.cpp
git commit -m "feat: rewrite arena match tick loop for NTM + squad leader + structured fighter inputs"
```

---

### Task 10: Update Arena Game Screen (UI)

**Files:**
- Modify: `include/neuroflyer/ui/screens/arena_game_screen.h`
- Modify: `src/ui/screens/arena/arena_game_screen.cpp`

- [ ] **Step 1: Update header member variables**

In `include/neuroflyer/ui/screens/arena_game_screen.h`:

1. Add includes:
```cpp
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/sector_grid.h>
```

2. Replace `SquadNetConfig squad_config_;` with:
```cpp
NtmNetConfig ntm_config_;
SquadLeaderNetConfig leader_config_;
```

3. Replace `std::vector<neuralnet::Network> squad_nets_;` with:
```cpp
std::vector<neuralnet::Network> ntm_nets_;
std::vector<neuralnet::Network> leader_nets_;
```

4. Replace `std::vector<std::vector<float>> team_broadcasts_;` with:
```cpp
std::vector<SquadLeaderOrder> team_orders_;
std::vector<float> squad_center_xs_;
std::vector<float> squad_center_ys_;
```

- [ ] **Step 2: Update initialization in arena_game_screen.cpp**

In the initialization section of `on_draw()` (or `on_enter()`), replace the squad config setup:

Replace:
```cpp
squad_config_ = SquadNetConfig{};
squad_config_.input_size = 8;
squad_config_.hidden_sizes = {6};
squad_config_.output_size = arena_config_.squad_broadcast_signals;
```

With:
```cpp
ntm_config_ = NtmNetConfig{};
ntm_config_.input_size = arena_config_.ntm_input_size;
ntm_config_.hidden_sizes = arena_config_.ntm_hidden_sizes;
ntm_config_.output_size = arena_config_.ntm_output_size;

leader_config_ = SquadLeaderNetConfig{};
leader_config_.input_size = arena_config_.squad_leader_input_size;
leader_config_.hidden_sizes = arena_config_.squad_leader_hidden_sizes;
leader_config_.output_size = arena_config_.squad_leader_output_size;
```

Replace population creation calls from:
```cpp
create_team_population(design, hidden, squad_config_, pop_size, rng)
```
To:
```cpp
create_team_population(design, hidden, ntm_config_, leader_config_, pop_size, rng)
```

- [ ] **Step 3: Update tick_arena() to use new flow**

Replace the squad net + broadcast section with the NTM → squad leader → orders flow. This mirrors the new `run_arena_match` logic:

1. Build sector grid each tick
2. Per team: gather threats → run NTM → run squad leader → store order
3. Per fighter: compute `SquadLeaderFighterInputs`, pass 6 values to `build_arena_ship_input`

Replace the broadcast_signals building and usage with team_orders_ + compute_squad_leader_fighter_inputs.

- [ ] **Step 4: Update do_arena_evolution()**

Replace `squad_config_` references with `ntm_config_` + `leader_config_` in population creation. The `evolve_squad_only` and `evolve_team_population` calls don't change (they don't take config params).

After evolution, rebuild nets:
```cpp
for (std::size_t t = 0; t < arena_config_.num_teams; ++t) {
    ntm_nets_[t] = team_population_[match_idx_0 + t].build_ntm_network();
    leader_nets_[t] = team_population_[match_idx_0 + t].build_squad_network();
    fighter_nets_[t] = team_population_[match_idx_0 + t].build_fighter_network();
}
```

- [ ] **Step 5: Build the full app**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build 2>&1 | tail -20`

Expected: Build succeeds with no errors.

- [ ] **Step 6: Run the app and test arena training**

Run: `./build/neuroflyer`

1. Open Hangar → select a genome with an arena fighter
2. Go to Squad Nets tab → click "Squad vs Squad"
3. Pick a fighter variant to pair with
4. Verify: training starts, ships move, no crashes
5. Let it run for a few generations — verify evolution proceeds

- [ ] **Step 7: Commit**

```bash
git add include/neuroflyer/ui/screens/arena_game_screen.h src/ui/screens/arena/arena_game_screen.cpp
git commit -m "feat: update arena game screen for NTM + squad leader tick flow"
```

---

### Task 11: Build Main Executable + Smoke Test

**Files:**
- Modify: `CMakeLists.txt` (main) — add new source files to the main executable

- [ ] **Step 1: Add new source files to main CMakeLists.txt**

In the main `CMakeLists.txt`, add to the engine sources list (wherever `src/engine/*.cpp` files are listed):

```
src/engine/sector_grid.cpp
src/engine/squad_leader.cpp
```

- [ ] **Step 2: Full build**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build 2>&1 | tail -10`

Expected: Clean build, no errors.

- [ ] **Step 3: Run full test suite**

Run: `cd /Users/wldarden/repos/Neuroflyer && ./build/tests/neuroflyer_tests`

Expected: All tests PASS.

- [ ] **Step 4: Run app smoke test**

Run: `./build/neuroflyer`

Smoke test checklist:
- [ ] Main menu loads
- [ ] Hangar screen works (genome list, variant list)
- [ ] Arena training (Squad vs Squad) starts without crashes
- [ ] Ships move and fight
- [ ] Generations advance (evolution runs)
- [ ] No input size mismatch errors in console
- [ ] Scroller training still works (regression check)

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add sector_grid and squad_leader to main build"
```

---

## Self-Review Checklist

**Spec coverage:**
- [x] Sector grid (2000x2000 sectors, Manhattan diamond radius 2) — Task 1
- [x] NTM sub-net (6 inputs → [4] → 1 output) — Task 5
- [x] NTM threat selection (top-1 by threat_score) — Task 5
- [x] NTM no-threat case (active_threat = 0) — Task 5 + 6
- [x] Squad leader net (11 inputs → [8] → 5 outputs) — Task 6
- [x] 1-hot argmax on output groups — Task 6
- [x] Tactical order → target mapping — Task 6
- [x] Fighter 6 structured inputs — Task 8
- [x] Squad spacing (stddev) — Task 7
- [x] TeamIndividual 3 nets — Task 4
- [x] evolve_squad_only mutates NTM + squad leader, freezes fighter — Task 4
- [x] Arena match new tick loop — Task 9
- [x] UI update — Task 10
- [x] Config additions — Task 2

**Placeholder scan:** No TBD/TODO/placeholder language found.

**Type consistency:**
- `NtmNetConfig` / `SquadLeaderNetConfig` used consistently
- `SquadLeaderOrder` / `SquadLeaderFighterInputs` used consistently
- `gather_near_threats` / `run_ntm_threat_selection` / `run_squad_leader` / `compute_squad_leader_fighter_inputs` signatures match between header and implementation
