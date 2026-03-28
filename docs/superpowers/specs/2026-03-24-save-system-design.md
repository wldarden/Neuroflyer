# NeuroFlyer Save System — Design Spec

**Date:** 2026-03-24
**Status:** Draft
**Scope:** `neuroflyer/` — save/load format, genome/variant organization, lineage tracking, UI flow

## Problem

The current save system has no structure:
- One flat directory of `.bin` files with no metadata
- No version field — format changes silently corrupt files
- Saves entire populations (100 nearly-identical individuals) when only the best matters
- Vision type, sensor layout, and memory slots are on `GameConfig`, not on the saved net — loading a net into a different config silently produces wrong inputs
- File listing code is duplicated 4x across main.cpp
- No concept of "this net was derived from that net" — all saves are flat peers

## Goals

1. **Genome/Variant hierarchy** — users create named Genomes (structure blueprints), evolve named Variants (trained individuals) from them. Organization is a tree, not a flat list.
2. **Self-describing saves** — a saved file contains everything needed to reconstruct and run the net: sensor layout, topology, weights, memory slots. No external config dependency.
3. **Versioned binary format** — future-proof with a version field.
4. **Lineage tracking** — a tree of parent→child relationships between saved variants.
5. **No population persistence** — populations are ephemeral training artifacts. Only individual snapshots are saved.
6. **Single source of truth for save/load** — one API, used by all UI screens and headless mode.

## Non-Goals

- Fitness function config in the save file (that's a training parameter, not a net property)
- Saving/restoring entire populations
- Undo/redo within lineage
- Cross-genome comparison tools (future work)

---

## Design

### 1. Conceptual Model

**Snapshot** — The universal save unit. Contains everything about one neural net: sensor layout (ship design), network topology, weights, memory slot count, and metadata. Both Genomes and Variants are Snapshots. The binary format is identical for both.

**Genome** — A Snapshot that serves as the root of a variant group. It represents the original structure with initial random weights. Users name it ("Lotus", "TripleThreat"). It defines the sensor layout, hidden layer structure, and memory slot count.

**Variant** — A Snapshot that is a child of a Genome (or another Variant). It represents a trained/evolved individual. Its topology may have mutated (added nodes/layers), but it still belongs to its parent Genome's group for organizational purposes.

**Population** — Ephemeral. Created at the start of a training session by either (a) random instantiation from a Genome's structure or (b) seeding from a Variant. Never persisted. The best individual can be saved as a new Variant at any time during training.

**Promotion** — "Create Genome from Variant" takes a Variant and creates a new Genome group using the Variant's current state as the new Genome's root snapshot. This is a copy + new directory — the original Variant stays in its parent Genome's group.

### 1.5. Ship Design Types

`ShipDesign` and `SensorDef` live in a shared header (`neuroflyer/include/neuroflyer/ship_design.h`) used by both the save system and the input encoding system.

```cpp
// neuroflyer/include/neuroflyer/ship_design.h
namespace neuroflyer {

enum class SensorType : uint8_t { Raycast = 0, Occulus = 1 };

struct SensorDef {
    SensorType type;
    float angle;            // radians relative to ship heading
    float range;            // max detection distance
    float width;            // Occulus minor axis ratio; ignored for Raycast
    bool is_full_sensor;    // true = dist+danger+value+coin (4 inputs), false = distance only (1 input)
};

struct ShipDesign {
    std::vector<SensorDef> sensors;
    uint16_t memory_slots = 4;
};

} // namespace neuroflyer
```

Input size is derived: `sum(sensor.is_full_sensor ? 4 : 1 for each sensor) + 3 (pos x, y, speed) + memory_slots`.
Output size is: `ACTION_COUNT (5) + memory_slots`.

### 2. Directory Structure

```
data/genomes/
├── Lotus/
│   ├── genome.bin              ← root Snapshot (initial random weights + structure)
│   ├── lineage.json            ← cached tree index (rebuildable from .bin headers)
│   ├── Lotus Variant.bin       ← evolved Snapshot (parent: genome.bin)
│   └── Lotus Test 2.bin        ← another evolved Snapshot (parent: genome.bin)
├── TripleThreat/
│   ├── genome.bin
│   ├── lineage.json
│   ├── TT-night.bin
│   └── BigT.bin
```

- Each Genome gets a directory named after it
- `genome.bin` is always the root Snapshot
- Variant files are named `{variant-name}.bin` in the same directory
- Lineage is stored in two complementary places:
  - Each `.bin` file stores its `parent_name` (singly-linked, write-once, never modified)
  - `lineage.json` stores the full tree (children relationships, MRCA stubs), rebuildable from `.bin` headers
- Directory name = Genome name (filesystem-safe characters)

### 3. Binary Save Format

Magic: `NFS\0` (0x4E465300) — new magic, distinct from the legacy `NFLY` (0x4E464C59) population format. This prevents old code from silently misparsing new files.

```
[magic: uint32 = 0x4E465300]
[version: uint16]
[crc32: uint32]             ← CRC32 of everything after this field (detects corruption/truncation)
[payload...]
```

#### Version 1 Payload

```
--- Header ---
[name_len: uint16] [name: utf8 bytes]     ← Snapshot name
[generation: uint32]                        ← generation count when saved (0 for fresh genome)
[created_timestamp: int64]                  ← Unix seconds since epoch

--- Lineage ---
[parent_name_len: uint16] [parent_name: utf8 bytes]   ← parent Snapshot name ("" if root genome)

--- Ship Design (Sensor Layout) ---
[num_sensors: uint16]
[memory_slots: uint16]
For each sensor:
    [type: uint8]           ← 0 = Raycast, 1 = Occulus
    [angle: float32]        ← radians, relative to ship heading
    [range: float32]        ← max detection distance
    [width: float32]        ← Occulus minor axis ratio; ignored for Raycast
    [is_full_sensor: uint8] ← 1 = full info (dist+danger+value+coin), 0 = distance only

--- Network Topology ---
[input_size: uint32]        ← derived from sensors + memory, written for validation
[num_layers: uint32]
For each layer:
    [output_size: uint32]
    [activation: uint32]    ← enum as int (0=ReLU, 1=Sigmoid, 2=Tanh)

--- Weights ---
[weight_count: uint32]
[weights: float32 * weight_count]
```

Each `.bin` file stores only its `parent_name` (singly-linked). Children relationships and MRCA stubs are tracked in `lineage.json`. Once written, `.bin` files are never modified — this prevents corruption of existing saves. See Section 4 for full lineage details.

Input size is **derived** from the ship design: each distance-only sensor contributes 1 input, each full sensor contributes 4, plus 3 (position x, y, speed), plus `memory_slots`. It's written to the file for validation on load (if computed input size doesn't match stored value, the file is corrupt or the derivation logic changed).

Output size is `ACTION_COUNT (5) + memory_slots`.

### 4. Lineage Tracking

Lineage is stored using two complementary mechanisms:

**1. Each `.bin` file stores its parent name** (singly-linked, write-once, never modified):
- `genome.bin`: parent="" (root)
- `Lotus Variant.bin`: parent="Lotus"
- `Lotus V1 Evolved.bin`: parent="Lotus Variant"
- `Lotus Test 2.bin`: parent="Lotus"

**2. `lineage.json` stores the full tree** (children relationships, metadata, MRCA stubs):

`.bin` files are **append-only** — once written, never modified. This eliminates the risk of corrupting an existing file when saving a new variant. `lineage.json` is the only file that gets rewritten, and it can always be rebuilt from `.bin` headers if lost or corrupted.

**Saving a new Variant:**
1. Write the new `.bin` via write-to-temp-then-rename (atomic on most filesystems)
2. Update `lineage.json` to include the new node

**Deleting a Variant:**
1. Show confirmation dialog listing affected children
2. Re-parent the deleted Variant's children to the deleted Variant's parent in `lineage.json`
3. Delete the `.bin` file
4. Root `genome.bin` cannot be deleted through this flow

**Rebuilding `lineage.json`:** Scan all `.bin` files, read each header's `parent_name`, reconstruct the tree from parent pointers. MRCA stub nodes (no `.bin` file) are lost on rebuild — acceptable since they are supplementary data.

**Name validation:** Genome and Variant names must be filesystem-safe: alphanumeric, spaces, hyphens, underscores only. Max 64 characters. Empty names rejected. Names that collide with existing files in the directory rejected. Path separators and OS-reserved names (CON, NUL, etc.) rejected.

**Consistency:** When saving a new Variant, two files are written: the new Variant (with its parent set) and the parent (with the new child appended). If the process crashes between the two writes, the child file exists but the parent doesn't list it. On load, the tree reconstruction detects orphans (files whose parent doesn't list them as a child) and repairs the links.

**Multi-save from one session (MRCA tracking):**

When a user saves multiple individuals from the same training session, the lineage tree should show where they diverged — not just that they're siblings under the session parent. This requires tracking the Most Recent Common Ancestor (MRCA).

**How it works:** Each elite individual carries a `primary_lineage` — a list of ancestor IDs, one per generation, following the fitter-parent chain through crossover. This is a flattened view of the real DAG (which is complex due to crossover); we always follow the fitter parent to keep it a simple chain.

```
Elite A lineage: [parent_var, gen1_e1, gen2_e3, gen3_e3, gen4_e7, ...]
Elite B lineage: [parent_var, gen1_e1, gen2_e3, gen3_e5, gen4_e1, ...]
                                                ^^ first divergence at gen 3
                 MRCA = gen2_e3 (last common ancestor)
```

**Storage cost:** Only elite individuals (default 10) have lineage tracked. Each generation appends 1 ID per elite. After 1000 generations: 10 lists of 1000 uint32_t = 40KB total. Non-elite individuals are ephemeral and don't carry lineage.

**On multi-save:** When the user saves N individuals from one session:

1. For each pair of saved individuals, find their MRCA by walking their `primary_lineage` lists forward and finding the last index where they agree.
2. Build a sub-tree from the pairwise MRCAs. For 3 saves (A, B, C) where MRCA(A,B) = gen 50 and MRCA(A,C) = gen 20:
   ```
   session_parent
   └── (MRCA all three, gen 20)
       ├── C
       └── (MRCA A+B, gen 50)
           ├── A
           └── B
   ```
3. MRCA nodes are inserted into the lineage tree as unnamed intermediate nodes (e.g., name = "(branch gen 50)"). They don't have `.bin` files — they're lineage-only entries in `lineage.json` with `"file": null`. The binary lineage links in the actual saved `.bin` files point through the intermediate MRCA nodes.

**Crossover handling:** When an offspring is produced by crossover of parents P1 (fitter) and P2, the offspring inherits P1's `primary_lineage` and appends P1's ID. This means the lineage tracks the fitter-parent chain, which is the dominant genetic contributor. The secondary parent's contribution is not tracked (it would require a full DAG, which is overkill).

**When not needed:** If the user only saves 1 individual per session, no MRCA computation is needed — the saved variant's parent is simply the session's seed variant.

#### In-Memory MRCA Tracking During Training

During a training run, each elite individual carries a `primary_lineage` chain (following the fitter parent through crossover). We store a **full snapshot** (topology + sensors + weights) for each unique ancestor entry, deduplicated across elites.

**Deduplication:** Only store a new entry when an elite slot changes occupant. If an elite survives S generations, that's 1 entry instead of S. Typical S = 20-30 at default settings.

**Pruning:** After each generation, drop snapshots that appear in only 0 or 1 current elite lineage chains — they can never be an MRCA between two saved individuals. The live set is bounded by the coalescence depth (~E × S entries). For E=10, S=25: ~250 live entries, ~750KB at 3KB per snapshot.

**Memory budget and graceful degradation:**

Two user-configurable parameters control MRCA tracking (exposed in `GameConfig` and tunable via the pause/config screen):

| Parameter | Default | Description |
|-----------|---------|-------------|
| `mrca_memory_limit_mb` | 64 | Maximum MB of RAM for in-memory MRCA snapshot storage |
| `mrca_prune_interval` | 20 | Run lineage pruning (drop entries in ≤1 elite chain) every N generations |

Under normal conditions (E=10, S=25, typical network sizes), the live set stays well under 1MB. The limit is a safety net for extreme edge cases (very large networks, low S, high E). The prune interval controls how often dead entries are cleaned up — lower values use less memory but do slightly more work per generation.

**Important:** When the user triggers a save, a forced prune + MRCA computation runs immediately (regardless of the prune interval). This prevents a stale prune from dropping an ancestor that turns out to be the MRCA of the individuals being saved. The interval-based pruning is an optimization for steady-state memory; save-time pruning is the correctness guarantee.

When the memory budget is exceeded, full snapshots are **downgraded** to lightweight stubs:

```cpp
struct MrcaEntry {
    enum class Level { Full, TopologyOnly };

    Level level;
    uint32_t generation;
    uint32_t individual_id;

    // Always present (both levels):
    neuralnet::NetworkTopology topology;   // ~30-50 bytes
    ShipDesign ship_design;                // ~200 bytes
    // Present only at Level::Full:
    std::vector<float> weights;            // ~3-8KB, cleared when downgraded
};
```

**Downgrade order: least-isolated first.** For each full-snapshot entry, compute its "isolation score" = `min(generation_gap_to_previous_entry, generation_gap_to_next_entry)`. The entry with the lowest isolation score is degraded first — it's the one whose loss is least costly because a nearby neighbor preserves nearly identical information. This maximizes temporal coverage across the surviving entries, like choosing optimal keyframes.

```
Example: budget exceeded, must degrade one entry.

  Gen 100 (score=887)   Gen 987 (score=2)   Gen 989 (score=2)   Gen 4000 (score=3011)
                                ↑ evict this — nearest neighbor is 2 gens away

After: Gen 100, Gen 989, Gen 4000 — nearly identical coverage.
```

After downgrade, the entry remains in the lineage chain (still a valid MRCA candidate for branch-point detection), but if saved to the lineage tree, the resulting MRCA node will be a topology-only stub — visible on the lineage tree with its network shape, but not loadable as a runnable variant.

On the lineage tree, these appear as:

```
Lotus (genome) ─── full variant, loadable
├── (branch gen 212: 36→12→9→10) ─── topology only, viewable but not loadable
│   ├── Lotus V1 (gen 847) ─── full variant, loadable
│   └── Lotus V2 (gen 640) ─── full variant, loadable
└── Lotus Test 2 (gen 312) ─── full variant, loadable
```

This guarantees: **the system can never run out of memory from MRCA tracking.** Under pressure, it degrades gracefully from "full runnable snapshots" to "topology shape + generation label" to (at absolute minimum) just "MRCA: Gen N" nodes. The tree structure is always preserved.

**Redundant `lineage.json` index:** In addition to the embedded links in each `.bin` file, a `lineage.json` file is maintained in each genome's directory as a convenient index. It caches the tree structure so the UI can display the lineage without reading every `.bin` header. The `.bin` files are the source of truth — `lineage.json` can be regenerated from them at any time by scanning headers.

```json
{
  "nodes": [
    {"name": "Lotus", "file": "genome.bin", "parent": null, "generation": 0, "created": "2026-03-24T14:30:00Z"},
    {"name": "Lotus Variant", "file": "Lotus Variant.bin", "parent": "Lotus", "generation": 847, "created": "2026-03-24T22:15:00Z"},
    {"name": "(branch gen 412)", "file": null, "parent": "Lotus Variant", "generation": 412, "created": "2026-03-26T08:00:00Z",
     "mrca_stub": true, "topology_summary": "36→12→9→10"},
    {"name": "Lotus V1 Evolved", "file": "Lotus V1 Evolved.bin", "parent": "(branch gen 412)", "generation": 1203, "created": "2026-03-26T08:00:00Z"},
    {"name": "Lotus V1b", "file": "Lotus V1b.bin", "parent": "(branch gen 412)", "generation": 980, "created": "2026-03-26T08:10:00Z"},
    {"name": "Lotus Test 2", "file": "Lotus Test 2.bin", "parent": "Lotus", "generation": 312, "created": "2026-03-25T09:00:00Z"}
  ]
}
```

Nodes with `"file": null` and `"mrca_stub": true` are MRCA branch-point nodes computed at multi-save time. They appear on the lineage tree as topology-only entries (not loadable as variants). If the MRCA had a full snapshot at save time, its `.bin` file is saved alongside the variants and `"file"` is set to the filename.

When saving a new variant, only the new `.bin` and `lineage.json` are written — no existing `.bin` files are modified. If `lineage.json` is missing or corrupt, it's rebuilt from `.bin` headers (MRCA stubs are lost on rebuild).

### 5. UI Flow

#### Main Menu / Genome Selection

```
┌────────────────────────────────┐
│  NEUROFLYER                    │
│                                │
│  Your Genomes:                 │
│  ┌──────────────────────────┐  │
│  │ ▸ Lotus        (3 vars)  │  │
│  │   TripleThreat (2 vars)  │  │
│  │   Alpha        (0 vars)  │  │
│  └──────────────────────────┘  │
│                                │
│  [Create New Genome]           │
│  [Exit]                        │
└────────────────────────────────┘
```

- Lists all directories in `data/genomes/`
- Shows variant count per genome
- "Create New Genome" → genome creation screen (set name, configure sensors, layers, memory)

#### Genome Detail / Variant Selection

```
┌────────────────────────────────┐
│  ← Back         LOTUS          │
│                                │
│  Structure: 35 inputs, 2×12   │
│  hidden, 9 outputs, 4 memory   │
│                                │
│  Variants:                     │
│  ┌──────────────────────────┐  │
│  │ ▸ Lotus Variant  gen 847 │  │
│  │   Lotus Test 2   gen 312 │  │
│  └──────────────────────────┘  │
│                                │
│  [Train Fresh]  (from genome)  │
│  [Train from Selected Variant] │
│  [View Net]  [Test Bench]      │
│  [Create Genome from Variant]  │
│  [Lineage Tree]                │
│  [Delete Variant]              │
└────────────────────────────────┘
```

- Shows genome structure summary
- Lists variants with generation count
- "Train Fresh" → creates random population from genome structure → enters flight mode
- "Train from Selected Variant" → seeds population from variant → enters flight mode
- "View Net" / "Test Bench" → existing screens, using the selected variant
- "Create Genome from Variant" → promotes variant to new genome (copies data, creates new directory)

#### During Training (Pause Screen)

```
┌────────────────────────────────┐
│  Training: Lotus                │
│  Parent: Lotus Variant          │
│  Generation: 1203               │
│  Best Fitness: 356,479          │
│                                 │
│  [Save Best as New Variant]     │
│    Name: ___________________    │
│                                 │
│  [Resume]  [End Training]       │
│                                 │
│  Config...                      │
└─────────────────────────────────┘
```

- Shows which genome and parent variant this session is evolving from
- "Save Best as New Variant" → saves the current best individual as a new variant under this genome, added to lineage tree immediately
- "End Training" → returns to genome detail screen. If the user hasn't saved anything, all training data is discarded — no variants created, no lineage updates
- Config section for tuning fitness params, speed, etc. (existing functionality)

#### Auto-Save and Crash Recovery

During training, the system periodically writes an auto-save file:

```
data/genomes/Lotus/~autosave.bin
```

This file:
- Is written every `autosave_interval` generations (configurable, default 10) with the current best individual
- Written atomically via write-to-temp-then-rename (`~autosave.tmp` → `~autosave.bin`) to prevent corruption from mid-write crashes
- Contains the full Snapshot format including parent lineage link
- Is **not** added to the lineage tree or `lineage.json`
- Is **deleted** when the user intentionally leaves the training screen (via "End Training")

**Crash recovery:** On app startup, if a `~autosave.bin` file is found in any genome directory, it means the app closed unexpectedly during training. The system:
1. Detects the orphaned auto-save
2. Renames it to `autosave-{date}.bin` (e.g., `autosave-2026-03-24.bin`)
3. Adds it to the parent's children list and updates `lineage.json`
4. Notifies the user: "Recovered auto-save from Lotus (gen 1203)"

This ensures no training progress is lost to crashes, while keeping the lineage tree clean during normal operation.

#### Lineage Tree

A simple tree view showing the variant DAG:

```
┌────────────────────────────────┐
│  ← Back    LOTUS LINEAGE       │
│                                │
│  Lotus (genome)                │
│  ├── Lotus Variant (gen 847)   │
│  │   └── Lotus V1 Evolved     │
│  │       (gen 1203)            │
│  └── Lotus Test 2 (gen 312)   │
│                                │
└────────────────────────────────┘
```

Text-based tree for now. Could become a visual node graph later.

### 6. API

All save/load operations go through a single module. No direct filesystem access from main.cpp.

```cpp
// neuroflyer/include/neuroflyer/save_system.h

namespace neuroflyer {

/// A complete neural net snapshot — used for both Genomes and Variants.
struct Snapshot {
    std::string name;
    uint32_t generation = 0;
    int64_t created_timestamp = 0;

    // Lineage (singly-linked — parent only; children tracked in lineage.json)
    std::string parent_name;              // empty string for root genome

    // Ship and network
    ShipDesign ship_design;       // sensor layout + memory slots
    neuralnet::NetworkTopology topology;
    std::vector<float> weights;
};

/// Metadata for a genome (directory listing).
struct GenomeInfo {
    std::string name;             // directory name
    std::size_t variant_count;
};

/// Summary of a Snapshot read from its header (without loading full weights).
struct SnapshotHeader {
    std::string name;
    uint32_t generation = 0;
    int64_t created_timestamp = 0;
    std::string parent_name;      // empty for root genome
};

// --- Save/Load ---

void save_snapshot(const Snapshot& snapshot, const std::string& path);
[[nodiscard]] Snapshot load_snapshot(const std::string& path);

/// Read only the header (name, generation, lineage links) without loading weights.
/// Useful for building the lineage tree without loading every net into memory.
[[nodiscard]] SnapshotHeader read_snapshot_header(const std::string& path);

// --- Genome Management ---

/// List all genomes in the data directory.
[[nodiscard]] std::vector<GenomeInfo> list_genomes(const std::string& data_dir);

/// List all snapshot headers for a genome (genome.bin + all variants).
[[nodiscard]] std::vector<SnapshotHeader> list_variants(const std::string& genome_dir);

/// Create a new genome directory with an initial genome.bin.
void create_genome(const std::string& data_dir, const Snapshot& genome);

/// Save a variant into an existing genome's directory.
/// Updates parent's .bin children list AND lineage.json.
void save_variant(const std::string& genome_dir, const Snapshot& variant);

/// Rebuild lineage.json from .bin headers (repair/regenerate).
void rebuild_lineage(const std::string& genome_dir);

/// Promote a variant to a new genome (copy + new directory).
void promote_to_genome(const std::string& data_dir,
                       const std::string& source_genome_dir,
                       const std::string& variant_filename,
                       const std::string& new_genome_name);

// --- Utility ---

/// Compute input size from a ship design.
[[nodiscard]] std::size_t compute_input_size(const ShipDesign& design);

/// Compute output size (ACTION_COUNT + memory_slots).
[[nodiscard]] std::size_t compute_output_size(const ShipDesign& design);

/// Build a random Snapshot from a ShipDesign (for creating new genomes).
[[nodiscard]] Snapshot create_random_snapshot(
    const std::string& name,
    const ShipDesign& design,
    const std::vector<std::size_t>& hidden_layers,
    std::mt19937& rng);

/// Seed a population from a Snapshot (for training).
/// The seed Snapshot occupies the first elite slot unchanged.
/// Remaining slots are mutated copies with increasing mutation strength,
/// giving the population diversity around the seed point.
[[nodiscard]] std::vector<Individual> create_population_from_snapshot(
    const Snapshot& snapshot,
    std::size_t population_size,
    const EvolutionConfig& config,
    std::mt19937& rng);

/// Extract the best individual from a population as a Snapshot.
[[nodiscard]] Snapshot best_as_snapshot(
    const std::string& name,
    const std::vector<Individual>& population,
    const ShipDesign& design,
    uint32_t generation);

} // namespace neuroflyer
```

### 7. Migration

Old `.bin` files (NFLY magic, no version) are **not migrated**. The old `data/nets/` directory is ignored by the new system. Users start fresh with the new `data/genomes/` directory.

The old `save_population` / `load_population` functions in `evolution.h/.cpp` are deleted. All save/load goes through the new `save_system.h` API.

### 8. What Stays the Same

- `GameConfig` JSON for app settings (window size, scoring params, speed, etc.)
- `config.json` path and format
- Neural net library (`neuralnet::Network`, `neuralnet::NetworkTopology`) — unchanged
- Evolution library (`evolve::Genome`, `evolve::Population`) — unchanged
- Gameplay mechanics (towers, tokens, bullets, collision) — unchanged

### 9. What Changes

- Save format: versioned, self-describing, includes ship design
- Save granularity: individual snapshots, not populations
- Directory structure: `data/genomes/{name}/` instead of flat `data/nets/`
- UI: genome list → variant list → train, replacing the current flat file picker
- Auto-save during training: saves to a temp slot or auto-named variant, not the active file
- Population seeding: from a Snapshot, not from a loaded population file
- File listing: single function in `save_system.cpp`, not duplicated 4x

### 10. Interaction with Sub-project A (Input Encoding)

The `ShipDesign` struct (sensor list + memory slots) is the bridge between the save system and the input encoding system. The save system persists it; the input encoding system reads it to build input vectors. They share the `ShipDesign` type but are otherwise independent.

**Implementation order:**
1. Save system (this spec) — establishes `ShipDesign` and `Snapshot` types, versioned format
2. Input encoding extraction — uses `ShipDesign` to build inputs via `query_sensor` / `build_input`
3. Integration — wire everything together in main.cpp
