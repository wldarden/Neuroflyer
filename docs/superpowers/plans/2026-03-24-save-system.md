# NeuroFlyer Save System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace NeuroFlyer's flat population save system with a Genome/Variant hierarchy featuring versioned binary snapshots, lineage tracking, MRCA computation, and crash-safe auto-save.

**Architecture:** A `Snapshot` is the universal save unit (topology + weights + ship design + lineage link). Genomes are root Snapshots; Variants are children. `.bin` files are write-once (singly-linked parent pointer). `lineage.json` indexes the tree (children, MRCA stubs). MRCA tracking during training uses per-elite lineage chains with full snapshots, pruned and degraded under memory pressure. Populations are ephemeral — seeded from Snapshots, never persisted.

**Tech Stack:** C++20, CMake, GoogleTest, nlohmann/json, CRC32 (zlib or manual)

**Spec:** `neuroflyer/docs/superpowers/specs/2026-03-24-save-system-design.md`

---

## File Map

### New Files

| File | Responsibility |
|------|----------------|
| `include/neuroflyer/ship_design.h` | `SensorType`, `SensorDef`, `ShipDesign` structs + `compute_input_size`/`compute_output_size` |
| `include/neuroflyer/snapshot.h` | `Snapshot`, `SnapshotHeader`, `GenomeInfo` structs |
| `include/neuroflyer/snapshot_io.h` | `save_snapshot`, `load_snapshot`, `read_snapshot_header` — binary format with CRC32 |
| `src/snapshot_io.cpp` | Binary serialization implementation |
| `include/neuroflyer/genome_manager.h` | `list_genomes`, `list_variants`, `create_genome`, `save_variant`, `delete_variant`, `promote_to_genome`, `rebuild_lineage`, `recover_autosaves` |
| `src/genome_manager.cpp` | Directory operations, lineage.json read/write, crash recovery |
| `include/neuroflyer/name_validation.h` | `is_valid_name` |
| `include/neuroflyer/mrca_tracker.h` | `MrcaEntry`, `MrcaTracker` class — elite lineage tracking, pruning, degradation |
| `src/mrca_tracker.cpp` | MRCA tracking implementation |
| `tests/ship_design_test.cpp` | ShipDesign input/output size computation tests |
| `tests/snapshot_io_test.cpp` | Binary round-trip, CRC validation, corrupt file detection |
| `tests/genome_manager_test.cpp` | Directory ops, lineage.json, crash recovery |
| `tests/mrca_tracker_test.cpp` | Elite lineage tracking, pruning, MRCA computation, degradation |

### Modified Files

| File | Nature of Change |
|------|-----------------|
| `include/neuroflyer/evolution.h` | Add `create_random_snapshot`, `create_population_from_snapshot`, `best_as_snapshot`. Deprecate `save_population`/`load_population` (deleted in Task 9). |
| `src/evolution.cpp` | Implement Snapshot↔Individual conversion. |
| `include/neuroflyer/config.h` | Add `mrca_memory_limit_mb`, `mrca_prune_interval`, `autosave_interval`. Remove `last_save_name`. |
| `src/config.cpp` | Serialize new config fields. |
| `src/main.cpp` | Replace hangar/pause screen save/load UI with genome/variant flow. Wire auto-save + crash recovery. |
| `CMakeLists.txt` | Add new `.cpp` source files to `neuroflyer` executable target. |
| `tests/CMakeLists.txt` | Add new test files AND new `../src/*.cpp` sources (test target compiles sources directly, not via library link). |

### Deleted Files

| File | Reason |
|------|--------|
| (none — old save/load code is in evolution.cpp, removed in-place) | |

---

## Phase 1: Core Types + Binary Format

### Task 1: ShipDesign types and size computation

**Files:**
- Create: `include/neuroflyer/ship_design.h`
- Create: `tests/ship_design_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write tests for `compute_input_size` and `compute_output_size`**

```cpp
// tests/ship_design_test.cpp
#include <neuroflyer/ship_design.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(ShipDesignTest, InputSize_AllFullSensors) {
    nf::ShipDesign design;
    design.memory_slots = 4;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true},  // 4 inputs
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, true},  // 4 inputs
    };
    // 2 full sensors × 4 + 3 (pos x, y, speed) + 4 memory = 15
    EXPECT_EQ(nf::compute_input_size(design), 15u);
}

TEST(ShipDesignTest, InputSize_MixedSensors) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, false},  // 1 input (distance only)
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, true},   // 4 inputs
        {nf::SensorType::Occulus, 1.0f, 120.0f, 0.3f, true},   // 4 inputs
    };
    // 1 + 4 + 4 + 3 + 2 = 14
    EXPECT_EQ(nf::compute_input_size(design), 14u);
}

TEST(ShipDesignTest, InputSize_NoSensors) {
    nf::ShipDesign design;
    design.memory_slots = 0;
    // Just 3 (pos x, y, speed) + 0 memory = 3
    EXPECT_EQ(nf::compute_input_size(design), 3u);
}

TEST(ShipDesignTest, OutputSize) {
    nf::ShipDesign design;
    design.memory_slots = 4;
    // ACTION_COUNT (5) + 4 memory = 9
    EXPECT_EQ(nf::compute_output_size(design), 9u);
}

TEST(ShipDesignTest, OutputSize_ZeroMemory) {
    nf::ShipDesign design;
    design.memory_slots = 0;
    EXPECT_EQ(nf::compute_output_size(design), 5u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests 2>&1 && ./build/neuroflyer/tests/neuroflyer_tests --gtest_filter=ShipDesign*`

Expected: Compilation failure — `ship_design.h` doesn't exist yet.

- [ ] **Step 3: Implement `ship_design.h`**

```cpp
// include/neuroflyer/ship_design.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace neuroflyer {

inline constexpr std::size_t ACTION_COUNT = 5;

enum class SensorType : uint8_t { Raycast = 0, Occulus = 1 };

struct SensorDef {
    SensorType type;
    float angle;
    float range;
    float width;
    bool is_full_sensor;
};

struct ShipDesign {
    std::vector<SensorDef> sensors;
    uint16_t memory_slots = 4;
};

[[nodiscard]] inline std::size_t compute_input_size(const ShipDesign& design) {
    std::size_t size = 3;  // pos_x, pos_y, speed
    for (const auto& s : design.sensors) {
        size += s.is_full_sensor ? 4 : 1;
    }
    size += design.memory_slots;
    return size;
}

[[nodiscard]] inline std::size_t compute_output_size(const ShipDesign& design) {
    return ACTION_COUNT + design.memory_slots;
}

} // namespace neuroflyer
```

- [ ] **Step 4: Add test file to CMakeLists.txt and run tests**

Add `ship_design_test.cpp` to the test sources in `tests/CMakeLists.txt`.

Run: `cmake --build build --target neuroflyer_tests 2>&1 && ./build/neuroflyer/tests/neuroflyer_tests --gtest_filter=ShipDesign*`

Expected: All 5 tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/ship_design.h tests/ship_design_test.cpp tests/CMakeLists.txt
git commit -m "feat(neuroflyer): add ShipDesign types with input/output size computation"
```

---

### Task 2: Snapshot struct and binary serialization

**Files:**
- Create: `include/neuroflyer/snapshot.h`
- Create: `include/neuroflyer/snapshot_io.h`
- Create: `src/snapshot_io.cpp`
- Create: `tests/snapshot_io_test.cpp`
- Modify: `CMakeLists.txt` (add `snapshot_io.cpp` to `antsim_core` sources... wait, this is neuroflyer, not antsim)

Note: NeuroFlyer's core lib is built directly in the main CMakeLists.txt or as part of the neuroflyer target. Check the existing build structure.

- [ ] **Step 1: Write round-trip test**

```cpp
// tests/snapshot_io_test.cpp
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <gtest/gtest.h>
#include <sstream>

namespace nf = neuroflyer;

TEST(SnapshotIOTest, RoundTrip_BasicSnapshot) {
    nf::Snapshot snap;
    snap.name = "TestNet";
    snap.generation = 42;
    snap.created_timestamp = 1711300000;
    snap.parent_name = "ParentNet";
    snap.ship_design.memory_slots = 4;
    snap.ship_design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true},
        {nf::SensorType::Raycast, 1.5f, 200.0f, 0.0f, false},
        {nf::SensorType::Occulus, 0.5f, 120.0f, 0.3f, true},
    };
    snap.topology.input_size = nf::compute_input_size(snap.ship_design);
    snap.topology.layers = {
        {12, neuralnet::Activation::Tanh},
        {9, neuralnet::Activation::Tanh},
    };
    snap.weights.resize(12 * 12 + 12 + 9 * 12 + 9, 0.5f);  // fill with 0.5

    std::stringstream ss;
    nf::save_snapshot(snap, ss);

    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    EXPECT_EQ(loaded.name, "TestNet");
    EXPECT_EQ(loaded.generation, 42u);
    EXPECT_EQ(loaded.created_timestamp, 1711300000);
    EXPECT_EQ(loaded.parent_name, "ParentNet");
    EXPECT_EQ(loaded.ship_design.memory_slots, 4);
    ASSERT_EQ(loaded.ship_design.sensors.size(), 3u);
    EXPECT_EQ(loaded.ship_design.sensors[0].type, nf::SensorType::Raycast);
    EXPECT_TRUE(loaded.ship_design.sensors[0].is_full_sensor);
    EXPECT_FALSE(loaded.ship_design.sensors[1].is_full_sensor);
    EXPECT_EQ(loaded.ship_design.sensors[2].type, nf::SensorType::Occulus);
    EXPECT_FLOAT_EQ(loaded.ship_design.sensors[2].width, 0.3f);
    EXPECT_EQ(loaded.topology.input_size, snap.topology.input_size);
    ASSERT_EQ(loaded.topology.layers.size(), 2u);
    EXPECT_EQ(loaded.topology.layers[0].output_size, 12u);
    EXPECT_EQ(loaded.weights.size(), snap.weights.size());
    EXPECT_FLOAT_EQ(loaded.weights[0], 0.5f);
}

TEST(SnapshotIOTest, RoundTrip_RootGenome) {
    nf::Snapshot snap;
    snap.name = "MyGenome";
    snap.generation = 0;
    snap.parent_name = "";  // root
    snap.ship_design.memory_slots = 0;
    snap.topology.input_size = 3;
    snap.topology.layers = {{5, neuralnet::Activation::ReLU}};
    snap.weights.resize(3 * 5 + 5, 0.1f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    EXPECT_EQ(loaded.parent_name, "");
    EXPECT_EQ(loaded.generation, 0u);
}

TEST(SnapshotIOTest, CorruptMagic_Throws) {
    std::stringstream ss;
    uint32_t bad_magic = 0xDEADBEEF;
    ss.write(reinterpret_cast<const char*>(&bad_magic), sizeof(bad_magic));
    ss.seekg(0);
    EXPECT_THROW(nf::load_snapshot(ss), std::runtime_error);
}

TEST(SnapshotIOTest, CorruptCRC_Throws) {
    nf::Snapshot snap;
    snap.name = "Test";
    snap.topology.input_size = 3;
    snap.topology.layers = {{std::size_t{2}, neuralnet::Activation::Tanh}};
    snap.weights.resize(3 * 2 + 2, 0.0f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);

    // Corrupt a byte in the middle of the payload (not CRC field or last byte)
    std::string data = ss.str();
    std::size_t mid = 10 + data.size() / 2;  // skip header, corrupt mid-payload
    if (mid < data.size()) data[mid] ^= 0xFF;
    std::stringstream corrupted(data);
    EXPECT_THROW(nf::load_snapshot(corrupted), std::runtime_error);
}

TEST(SnapshotIOTest, FutureVersion_Throws) {
    nf::Snapshot snap;
    snap.name = "Test";
    snap.topology.input_size = 3;
    snap.topology.layers = {{std::size_t{2}, neuralnet::Activation::Tanh}};
    snap.weights.resize(3 * 2 + 2, 0.0f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);

    // Overwrite version field (bytes 4-5) with version=99
    std::string data = ss.str();
    uint16_t bad_version = 99;
    std::memcpy(&data[4], &bad_version, sizeof(bad_version));
    std::stringstream bad_ver(data);
    EXPECT_THROW(nf::load_snapshot(bad_ver), std::runtime_error);
}

TEST(SnapshotIOTest, ReadHeader_OnlyGetsMetadata) {
    nf::Snapshot snap;
    snap.name = "BigNet";
    snap.generation = 100;
    snap.created_timestamp = 1711300000;
    snap.parent_name = "Parent";
    snap.ship_design.memory_slots = 4;
    snap.topology.input_size = 7;
    snap.topology.layers = {{12, neuralnet::Activation::Tanh}};
    snap.weights.resize(100, 0.0f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto header = nf::read_snapshot_header(ss);

    EXPECT_EQ(header.name, "BigNet");
    EXPECT_EQ(header.generation, 100u);
    EXPECT_EQ(header.parent_name, "Parent");
    EXPECT_EQ(header.created_timestamp, 1711300000);
}
```

- [ ] **Step 2: Run tests to verify compilation failure**

- [ ] **Step 3: Create `snapshot.h`**

```cpp
// include/neuroflyer/snapshot.h
#pragma once
#include <neuroflyer/ship_design.h>
#include <neuralnet/network.h>
#include <cstdint>
#include <string>
#include <vector>

namespace neuroflyer {

struct Snapshot {
    std::string name;
    uint32_t generation = 0;
    int64_t created_timestamp = 0;
    std::string parent_name;

    ShipDesign ship_design;
    neuralnet::NetworkTopology topology;
    std::vector<float> weights;
};

struct SnapshotHeader {
    std::string name;
    uint32_t generation = 0;
    int64_t created_timestamp = 0;
    std::string parent_name;
};

struct GenomeInfo {
    std::string name;
    std::size_t variant_count = 0;
};

} // namespace neuroflyer
```

- [ ] **Step 4: Create `snapshot_io.h`**

```cpp
// include/neuroflyer/snapshot_io.h
#pragma once
#include <neuroflyer/snapshot.h>
#include <iosfwd>
#include <string>

namespace neuroflyer {

void save_snapshot(const Snapshot& snapshot, std::ostream& out);
[[nodiscard]] Snapshot load_snapshot(std::istream& in);
[[nodiscard]] SnapshotHeader read_snapshot_header(std::istream& in);

// File-based convenience overloads
void save_snapshot(const Snapshot& snapshot, const std::string& path);
[[nodiscard]] Snapshot load_snapshot(const std::string& path);
[[nodiscard]] SnapshotHeader read_snapshot_header(const std::string& path);

} // namespace neuroflyer
```

- [ ] **Step 5: Implement `snapshot_io.cpp`**

Implementation must:
- Write magic `0x4E465300`, version `1`, then CRC32 placeholder (4 bytes of 0)
- Write the payload: name (uint16 len + bytes), generation (uint32), timestamp (int64), parent_name (uint16 len + bytes), sensor count (uint16), memory_slots (uint16), each sensor (uint8 type + float32 angle + float32 range + float32 width + uint8 is_full), input_size (uint32), num_layers (uint32), each layer (uint32 output_size + uint32 activation), weight_count (uint32), weights (float32 × count)
- After writing all payload, compute CRC32 of the payload bytes, seek back, and overwrite the CRC32 field
- On load: read magic (validate), version (validate), stored CRC32, then payload. Compute CRC32 of payload and compare with stored value. Throw `std::runtime_error` on mismatch.
- For `read_snapshot_header`: read only up through `parent_name`, skip the rest. No CRC validation (would need full read).

Use a simple CRC32 implementation (public domain table-based, ~20 lines) to avoid an external dependency.

- [ ] **Step 6: Add `snapshot_io.cpp` to build, run tests**

Add to `CMakeLists.txt` source list. Run tests.

Expected: All 5 snapshot tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/neuroflyer/snapshot.h include/neuroflyer/snapshot_io.h src/snapshot_io.cpp tests/snapshot_io_test.cpp CMakeLists.txt
git commit -m "feat(neuroflyer): versioned binary snapshot format with CRC32

Magic NFS\\0 (0x4E465300), version 1. Self-describing: includes
ship design, topology, weights, lineage parent link. CRC32 detects
corruption."
```

---

### Task 3: Name validation

**Files:**
- Create: `include/neuroflyer/name_validation.h`
- Create: `tests/name_validation_test.cpp`

- [ ] **Step 1: Write tests**

```cpp
TEST(NameValidation, ValidNames) {
    EXPECT_TRUE(nf::is_valid_name("Lotus"));
    EXPECT_TRUE(nf::is_valid_name("Triple Threat"));
    EXPECT_TRUE(nf::is_valid_name("TT-night"));
    EXPECT_TRUE(nf::is_valid_name("net_v2"));
    EXPECT_TRUE(nf::is_valid_name("A"));
}

TEST(NameValidation, InvalidNames) {
    EXPECT_FALSE(nf::is_valid_name(""));
    EXPECT_FALSE(nf::is_valid_name("../../../etc/passwd"));
    EXPECT_FALSE(nf::is_valid_name("a/b"));
    EXPECT_FALSE(nf::is_valid_name(std::string(65, 'a')));  // too long
    EXPECT_FALSE(nf::is_valid_name("CON"));
    EXPECT_FALSE(nf::is_valid_name("NUL"));
}
```

- [ ] **Step 2: Implement as a header-only utility**

`is_valid_name`: check length 1-64, only alphanumeric/space/hyphen/underscore, no path separators, not a Windows reserved name.

- [ ] **Step 3: Run tests, commit**

---

## Phase 2: Genome/Variant Directory Management

### Task 4: Genome manager — create, list, lineage.json

**Files:**
- Create: `include/neuroflyer/genome_manager.h`
- Create: `src/genome_manager.cpp`
- Create: `tests/genome_manager_test.cpp`

- [ ] **Step 1: Write tests for `create_genome` and `list_genomes`**

Tests should use a temp directory (created in test setup, cleaned in teardown). Verify:
- `create_genome` creates the directory and writes `genome.bin` + `lineage.json`
- `list_genomes` returns the genome with correct name and variant_count=0
- Creating a second genome lists both
- Name validation rejects invalid names

- [ ] **Step 2: Write tests for `save_variant` and `list_variants`**

- `save_variant` writes the `.bin` file and updates `lineage.json`
- `list_variants` returns genome + variant headers
- Parent name is correctly set in the variant's binary
- `lineage.json` has both nodes with correct parent pointers

- [ ] **Step 3: Write tests for `delete_variant`**

- Deleting a leaf variant removes `.bin` and updates `lineage.json`
- Deleting a variant with children re-parents children to deleted variant's parent
- Attempting to delete `genome.bin` throws/returns error

- [ ] **Step 4: Write tests for `promote_to_genome`**

- Creates new directory with `genome.bin` copied from the variant
- New genome's parent_name is "" (root)
- Original variant still exists in source genome

- [ ] **Step 5: Write tests for `rebuild_lineage`**

- Delete `lineage.json`, call `rebuild_lineage`, verify tree is reconstructed from `.bin` headers
- MRCA stubs are lost (expected)

- [ ] **Step 6: Implement `genome_manager.h` and `genome_manager.cpp`**

Implementation details:
- `create_genome`: mkdir, save snapshot as `genome.bin`, write initial `lineage.json` with one root node
- `save_variant`: validate name, write `.bin` via temp+rename, read+update+write `lineage.json`
- `list_genomes`: scan `data/genomes/` for directories, count `.bin` files minus `genome.bin`
- `list_variants`: read all `.bin` headers in the genome directory
- `delete_variant`: validate not genome.bin, re-parent children in lineage.json, delete `.bin`
- `promote_to_genome`: load variant snapshot, clear parent_name, save as genome.bin in new dir
- `rebuild_lineage`: scan `.bin` headers, build tree from parent pointers, write lineage.json
- `lineage.json` format: `{"nodes": [{"name": "...", "file": "...", "parent": "...", "generation": N, "created": "..."}]}`

- [ ] **Step 7: Run all tests, commit**

```bash
git commit -m "feat(neuroflyer): genome/variant directory management with lineage.json"
```

---

### Task 5: Crash recovery (auto-save detection)

**Files:**
- Modify: `include/neuroflyer/genome_manager.h`
- Modify: `src/genome_manager.cpp`
- Modify: `tests/genome_manager_test.cpp`

- [ ] **Step 1: Write test for `recover_autosaves`**

- Place a `~autosave.bin` in a genome directory
- Call `recover_autosaves`
- Verify: file renamed to `autosave-{date}.bin`, added to lineage.json, original `~autosave.bin` gone
- Returns list of recovered variants for UI notification

- [ ] **Step 2: Implement `recover_autosaves`**

Scan all genome directories for `~autosave.bin`. For each found: read header to get parent_name, rename to `autosave-{date}.bin`, add to lineage.json.

- [ ] **Step 3: Run tests, commit**

---

## Phase 3: Population Seeding + Auto-Save

### Task 6: Snapshot ↔ Individual conversion

**Files:**
- Modify: `include/neuroflyer/evolution.h`
- Modify: `src/evolution.cpp`
- Create: `tests/snapshot_population_test.cpp`

- [ ] **Step 1: Write tests**

- `create_random_snapshot`: given a ShipDesign + hidden layers, produces a Snapshot with correct input_size, output_size, random weights, generation=0
- `best_as_snapshot`: create a population with known fitness values, extract best, verify name/generation/topology/weights match. Note: `Individual` stores weights as `evolve::Genome`; extraction uses `genome.genes()` to get the `std::vector<float>`.
- `create_population_from_snapshot`: create a snapshot, seed a population of 10, verify first individual matches snapshot exactly (elite copy via `evolve::Genome(snapshot.weights)`), remaining are mutated (different weights but same topology)
- Name collision test: `save_variant` with a name that already exists should throw or return error

- [ ] **Step 2: Implement `create_random_snapshot`, `best_as_snapshot`, `create_population_from_snapshot`**

`create_random_snapshot`: build topology from ShipDesign + hidden layers, generate random weights via `evolve::Genome::random`, set generation=0, timestamp=now.

`best_as_snapshot`: find max fitness, extract topology + `genome.genes()` as weights, set name/generation/ship_design.

`create_population_from_snapshot`: build Individual from snapshot (first slot = exact copy using `evolve::Genome(snapshot.weights)`), remaining slots = copies with mutations applied. Use increasing mutation strength for diversity (e.g., `base_strength * (1 + slot_idx * 0.5)`).

- [ ] **Step 3: Mark old `save_population` / `load_population` as deprecated**

Add `[[deprecated("Use snapshot_io + genome_manager instead")]]` to both declarations in `evolution.h`. Do NOT delete them yet — `main.cpp` still calls them. They will be deleted in Task 9 when all call sites are replaced.

- [ ] **Step 4: Run tests, commit**

```bash
git commit -m "feat(neuroflyer): Snapshot ↔ Individual conversion for population seeding

Adds create_random_snapshot, best_as_snapshot, create_population_from_snapshot.
Old save_population/load_population marked deprecated (deleted in Task 9)."
```

---

### Task 7: Auto-save during training

This task implements the write-to-temp-then-rename auto-save mechanism. It's a utility function that main.cpp will call; the actual call site is wired in Task 9.

**Files:**
- Modify: `include/neuroflyer/genome_manager.h`
- Modify: `src/genome_manager.cpp`
- Modify: `tests/genome_manager_test.cpp`

- [ ] **Step 1: Write tests**

- `write_autosave`: writes `~autosave.bin` atomically (via temp file)
- `delete_autosave`: removes `~autosave.bin` if it exists
- Auto-save file is a valid Snapshot (can be loaded)

- [ ] **Step 2: Implement**

`write_autosave(genome_dir, snapshot)`: save to `~autosave.tmp`, rename to `~autosave.bin`.
`delete_autosave(genome_dir)`: remove `~autosave.bin` if present.

- [ ] **Step 3: Run tests, commit**

---

## Phase 4: MRCA Tracking

### Task 8: MrcaTracker — elite lineage chains + pruning

**Files:**
- Create: `include/neuroflyer/mrca_tracker.h`
- Create: `src/mrca_tracker.cpp`
- Create: `tests/mrca_tracker_test.cpp`

- [ ] **Step 1: Write tests for basic lineage tracking**

- Create tracker with E=3 elites
- Record 5 generations of elite IDs with topologies
- Verify lineage chains are correct length
- Verify dedup: elite that survives 3 gens has 1 entry, not 3

- [ ] **Step 2: Write tests for pruning**

- Set up 3 elites with divergent lineages
- After enough generations, one ancestor only appears in 1 chain
- Call prune → that ancestor's full snapshot is dropped
- Verify entry still exists as TopologyOnly (for chain continuity)

- [ ] **Step 3: Write tests for MRCA computation**

- Set up 2 elites that share lineage until gen 5 then diverge
- Compute MRCA → returns gen 5 entry
- Set up 3 elites: A and B diverge at gen 10, A and C diverge at gen 3
- Compute pairwise MRCAs → builds correct tree structure

- [ ] **Step 4: Write tests for memory budget degradation**

- Create tracker with tiny memory budget (e.g., 1KB)
- Add many full-snapshot entries until budget exceeded
- Verify least-isolated entries are degraded first
- Verify degraded entries have Level::TopologyOnly

- [ ] **Step 5: Implement MrcaTracker**

```cpp
// include/neuroflyer/mrca_tracker.h
class MrcaTracker {
public:
    MrcaTracker(std::size_t elite_count, std::size_t memory_limit_bytes,
                std::size_t prune_interval);

    // Called each generation: record which individual occupies each elite slot
    void record_generation(uint32_t generation,
                           const std::vector<uint32_t>& elite_ids,
                           const std::vector<neuralnet::NetworkTopology>& topologies,
                           const std::vector<ShipDesign>& ship_designs,
                           const std::vector<std::vector<float>>& weights);

    // Periodic pruning (called every prune_interval generations)
    void prune();

    // Compute MRCA tree for a set of elite indices (called at save time)
    struct MrcaNode {
        uint32_t generation;
        MrcaEntry entry;        // Full or TopologyOnly
        std::vector<std::size_t> children;  // indices into result vector
    };
    [[nodiscard]] std::vector<MrcaNode> compute_mrca_tree(
        const std::vector<std::size_t>& elite_indices) const;

    // Memory pressure
    [[nodiscard]] std::size_t memory_usage_bytes() const;

private:
    // Per-elite lineage chain
    struct EliteChain {
        std::vector<uint32_t> ancestor_ids;  // one per generation
    };

    // Deduplicated entry store: id → MrcaEntry
    std::unordered_map<uint32_t, MrcaEntry> entries_;
    std::vector<EliteChain> chains_;
    // ...
};
```

Key implementation details:
- `record_generation`: for each elite slot, if the individual ID changed from last gen, store a new entry. Append to chain. Check memory budget → degrade if needed.
- `prune`: count references to each entry ID across all chains. Drop full snapshot data for entries with ≤1 reference (keep as TopologyOnly so chain structure is preserved).
- Memory degradation: compute isolation score for each full entry, degrade least-isolated first.
- `compute_mrca_tree`: for each pair of given elite indices, walk their chains forward to find last common ancestor. Build tree from pairwise MRCAs.

- [ ] **Step 6: Run all tests, commit**

```bash
git commit -m "feat(neuroflyer): MRCA tracker with pruning and graceful degradation"
```

---

## Phase 5: UI Integration

### Task 9a: Config changes + crash recovery on startup

**Files:**
- Modify: `include/neuroflyer/config.h`
- Modify: `src/config.cpp`
- Modify: `src/main.cpp` (startup section only)

- [ ] **Step 1: Add new config fields**

Add `mrca_memory_limit_mb` (default 64), `mrca_prune_interval` (default 20), `autosave_interval` (default 10) to `GameConfig`. Remove `last_save_name`. Add `active_genome` (string, name of last-used genome directory). Update `config.cpp` JSON serialization.

- [ ] **Step 2: Add crash recovery on startup**

In `main.cpp`, after loading config and before showing the main menu:
```cpp
auto recovered = neuroflyer::recover_autosaves(data_dir + "/genomes");
for (const auto& r : recovered) {
    std::cout << "Recovered auto-save: " << r << "\n";
}
```

- [ ] **Step 3: Build and test, commit**

```bash
git commit -m "feat(neuroflyer): add MRCA/autosave config fields + crash recovery on startup"
```

---

### Task 9b: Genome selection screen (replaces hangar)

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace `draw_hangar_screen` with genome selection**

Replace the current hangar screen function with a genome list screen:
- Call `list_genomes(data_dir + "/genomes")` to populate the list
- Show each genome with variant count
- "Create New Genome" button → genome creation flow. This is a new ImGui sub-screen where the user sets: name (text input), number of raycast sight sensors, number of raycast full sensors, memory slots, hidden layer sizes. These map to a `ShipDesign` + `hidden_layers` vector. On confirm: `create_random_snapshot` → `create_genome`.
- Selecting a genome → sets active genome, transitions to variant screen

- [ ] **Step 2: Build and test, commit**

```bash
git commit -m "feat(neuroflyer): genome selection screen replaces hangar"
```

---

### Task 9c: Variant selection screen

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement variant selection screen**

New screen (replaces the old net file picker within the hangar):
- Call `list_variants(genome_dir)` to populate variant list
- "Train Fresh" → `create_random_snapshot` from genome.bin's ship_design → `create_population_from_snapshot` → enter flight
- "Train from Variant" → `load_snapshot(variant_path)` → `create_population_from_snapshot` → enter flight
- "View Net" / "Test Bench" → existing screens, load the selected variant
- "Create Genome from Variant" → `promote_to_genome`, refresh genome list
- "Delete Variant" → `delete_variant` with confirmation dialog
- "Back" → return to genome list

- [ ] **Step 2: Build and test, commit**

```bash
git commit -m "feat(neuroflyer): variant selection screen with train/view/promote/delete"
```

---

### Task 9d: Pause screen rewrite + auto-save wiring

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Update pause screen (during training)**

Replace current save/load section with:
- Show genome name + parent variant name + generation count + best fitness
- "Save Best as New Variant" → name input + `best_as_snapshot` + `save_variant`
- "End Training" → `delete_autosave` + return to variant screen. If unsaved, no prompt (user chose to discard).
- Config section stays (existing functionality)

- [ ] **Step 2: Wire auto-save during training**

In the game loop, after each generation:
```cpp
if (generation % config.autosave_interval == 0) {
    auto snap = best_as_snapshot("~autosave", population, ship_design, generation);
    write_autosave(genome_dir, snap);
}
```

On clean exit from training: `delete_autosave(genome_dir)`.

- [ ] **Step 3: Build and test, commit**

```bash
git commit -m "feat(neuroflyer): pause screen save-as-variant + auto-save during training"
```

---

### Task 9e: MRCA wiring + lineage tree view

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Wire MRCA tracker during training**

Create `MrcaTracker` at the start of each training session. After each generation, call `record_generation` with elite IDs/topologies/weights. Every `prune_interval` generations, call `prune()`. At save time, if multiple saves from same session, call `compute_mrca_tree` and insert MRCA stubs into lineage.json.

- [ ] **Step 2: Implement lineage tree view screen**

Simple ImGui text tree accessible from the variant selection screen. Read `lineage.json`, render indented tree with node names and generation numbers. MRCA stubs shown as "(branch gen N: topology_summary)". Full variants are clickable (could select them).

- [ ] **Step 3: Build and test, commit**

```bash
git commit -m "feat(neuroflyer): MRCA wiring during training + lineage tree view"
```

---

### Task 9f: Delete old code + final verification

**Files:**
- Modify: `src/main.cpp`
- Modify: `include/neuroflyer/evolution.h`
- Modify: `src/evolution.cpp`

- [ ] **Step 1: Remove old save/load code**

Delete `save_population` and `load_population` from `evolution.h` and `evolution.cpp` (they were deprecated in Task 6). Delete all remaining references in `main.cpp`: `net_files`, `net_name_buf`, the old hangar file picker, old pause screen save section, duplicate file-listing code (4 copies → 0).

- [ ] **Step 2: Full build and test**

```bash
cmake --build build 2>&1 && ctest --test-dir build --output-on-failure
```

All tests must pass. Manual smoke test: run the app, create a genome, train, save a variant, exit, restart, verify the variant loads.

- [ ] **Step 3: Commit**

```bash
git commit -m "refactor(neuroflyer): remove legacy save system

Deleted save_population/load_population and all references.
Removed 4x duplicated file-listing code.
All save/load now goes through snapshot_io + genome_manager."
```

---

### Task 10: Update CLAUDE.md

- [ ] **Step 1: Update `neuroflyer/CLAUDE.md`**

- Update the save/load documentation to describe the new Genome/Variant system
- Document the binary format (NFS magic, versioned)
- Document the lineage tree and MRCA tracking
- Update the controls table if UI changed
- Remove references to `neuroflyer_pop.bin`

- [ ] **Step 2: Commit**

```bash
git commit -m "docs(neuroflyer): update CLAUDE.md for new save system"
```
