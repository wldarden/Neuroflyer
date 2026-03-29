# Team Genome UI — Net Type Tabs & Squad Training

**Date:** 2026-03-28
**Status:** Approved

## Overview

Genomes now represent teams with multiple net types (fighters, squad nets, and eventually commanders). The VariantViewerScreen gets a tab bar to browse and manage variants of each net type. Squad nets can be trained in isolation with a frozen fighter variant, evolving only the squad coordination strategy.

## Genome Filesystem Layout

Each net type gets its own subdirectory within the genome directory. Fighter variants stay at the top level for backward compatibility.

```
data/genomes/{genome_name}/
├── genome.bin                    # Root fighter variant (unchanged)
├── {fighter_variant}.bin         # Fighter variants (unchanged)
├── lineage.json                  # Fighter lineage (unchanged)
├── squad/                        # Squad net subdirectory
│   ├── {squad_variant}.bin       # Squad net snapshots
│   └── lineage.json              # Squad-specific lineage tree
├── commander/                    # Future: commander net subdirectory
│   ├── {cmd_variant}.bin
│   └── lineage.json
└── ~autosave.bin                 # Auto-save (unchanged)
```

Existing genomes without a `squad/` directory are fully backward compatible — they simply show no squad net variants.

## Snapshot Format Extension

The `Snapshot` struct gains one new optional field:

- `paired_fighter_name` — the name of the fighter variant this squad net was trained with. Empty string for fighter snapshots. Stored in binary format v5.

Binary format versioning: v5 adds a string field after the existing v4 data. The reader checks version and reads the field if present; v1-v4 snapshots read with `paired_fighter_name = ""`.

## VariantViewerScreen — Tab Bar

### Tab enum

```
enum class NetTypeTab { Fighters, SquadNets, Commander };
```

Stored as a member on VariantViewerScreen. Default: `Fighters`.

### Tab bar widget

Horizontal row of buttons at the top of the screen, above the variant list. Active tab has a colored underline. Commander tab is visible but grayed/disabled (future).

Colors per tab:
- Fighters: purple (#a29bfe) — matches existing net viewer color
- Squad Nets: yellow (#f9ca24) — matches squad broadcast color from arena design
- Commander: teal (#4ecdc4) — future

### Tab switching behavior

- Switching tabs reloads the variant list for that net type
- Selected variant resets to none
- Action panel updates to show net-type-specific actions
- Each tab maintains no persistent state between switches (list is re-read each time)

### Variant list per tab

**Fighters tab** (unchanged):

| Column | Source |
|--------|--------|
| Name | snapshot.name |
| Gen | snapshot.generation |
| Runs | snapshot.run_count |
| Created | snapshot.created_timestamp |
| Parent | snapshot.parent_name |

**Squad Nets tab:**

| Column | Source |
|--------|--------|
| Name | snapshot.name |
| Gen | snapshot.generation |
| Paired Fighter | snapshot.paired_fighter_name |
| Created | snapshot.created_timestamp |

### Action panel per tab

**Fighters tab** — unchanged from current implementation:
- Training (Scroller): Train Fresh, Train From Variant
- Training (Arena): Arena Free-for-All
- Inspection: View Neural Net, Sensor Testing, Lineage Tree
- Evolution Settings: evolvable flag checkboxes
- Management: Promote to New Genome, Delete Variant

**Squad Nets tab:**
- Training Scenarios: Squad vs Squad, Base Attack (Base Defense deferred)
- Fighter Pairing: shows currently paired fighter, [Change] button opens a fighter variant picker modal
- Inspection: View Squad Net (pushes VariantNetEditorScreen — works since it's a standard Network)
- Management: Delete Variant

## GenomeManager Extensions

New methods for squad variant management:

```
list_squad_variants(genome_dir) → vector<SnapshotHeader>
    Reads all .bin files from {genome_dir}/squad/

save_squad_variant(genome_dir, snapshot) → void
    Writes snapshot to {genome_dir}/squad/{name}.bin
    Creates squad/ directory if it doesn't exist
    Updates {genome_dir}/squad/lineage.json

delete_squad_variant(genome_dir, variant_name) → void
    Deletes {genome_dir}/squad/{name}.bin
    Updates lineage.json
```

These mirror the existing `list_variants`, `save_variant`, `delete_variant` functions but operate on the `squad/` subdirectory.

## Squad Net Training Flow

### User action

1. User is on Squad Nets tab, selects a squad variant (or none for Train Fresh — creates random squad nets using SquadNetConfig defaults: 8 inputs → [6] hidden → squad_broadcast_signals outputs)
2. Clicks a training scenario button (e.g., "Squad vs Squad")
3. If no fighter variant is paired, a modal pops up: fighter variant picker (list from same genome's Fighters tab)
4. User selects fighter variant → pairing is set
5. ArenaConfigScreen is pushed with scenario preset values
6. User adjusts config if desired, clicks Start
7. ArenaGameScreen launches in **squad training mode**

### Scenario presets

**Squad vs Squad:**
- num_teams = 2, num_squads = 1, fighters_per_squad = 8
- base_hp = 1000, tower_count = 50, token_count = 20
- time_limit_ticks = 3600 (60 seconds)

**Base Attack:**
- num_teams = 2, num_squads = 1, fighters_per_squad = 8 for team 0 (attacker), fighters_per_squad = 0 for team 1 (no defenders — base only)
- Team 1's base exists but has no fighters. Implementation: ArenaSession already handles teams with 0 alive ships.
- time_limit_ticks = 1800 (30 seconds)
- Fitness: base damage dealt only

Note: `fighters_per_squad = 0` for the defender team means ArenaConfig needs to support asymmetric team sizes, or we use a separate `base_attack_mode` flag that spawns an undefended base. The simpler approach: use the standard 2-team config but manually kill all team 1 fighters at spawn. This avoids config changes.

### Squad training mode

ArenaGameScreen receives a flag indicating squad-only training:

- **Fighter weights frozen:** all fighters use the same weights loaded from the paired variant. Fighter weights are NOT mutated during evolution.
- **Squad net evolves:** population of `TeamIndividual` where only `squad_individual` mutates each generation. `fighter_individual` is a frozen copy reloaded from the paired variant every generation.
- **Evolution function:** `evolve_squad_only(population, config, rng)` — same as `evolve_team_population` but `apply_mutations` is only called on `squad_individual`, not `fighter_individual`.

### Saving squad variants

From the pause screen's "Save Variants" tab:
- Detects squad training mode
- Saves selected squad net variants to `{genome_dir}/squad/{name}.bin`
- Sets `paired_fighter_name` on the snapshot to the name of the paired fighter variant
- Updates `{genome_dir}/squad/lineage.json`

## Fighter Pairing Modal

A simple modal (subclass UIModal) that shows a list of fighter variants from the current genome. User clicks one to select it. The modal calls its `on_select` callback with the chosen variant name and closes.

Used when:
- User clicks [Change] on the fighter pairing in the Squad Nets action panel
- User clicks a training scenario button without a fighter paired

## Deferred Features

- **Commander tab** — shown grayed/disabled. Same pattern as Squad Nets when implemented.
- **Base Defense scenario** — needs AI attackers. Stub as unavailable button.
- **Cross-net-type lineage** — visualizing how squad nets relate to fighters they were trained with.
- **Promote squad net** — copying a squad variant to another genome.
- **Squad net sensor testing** — the squad net has no sensors (it takes stats), so the test bench doesn't apply. Could show a "Squad Stats Simulator" in the future.
- **Joint training** — evolving squad + fighter together (already works via `evolve_team_population`, just not exposed as a separate scenario yet).
