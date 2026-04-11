# Overnight Review — Mar 30, 04:46 PM

> **Scope:** Full NeuroFlyer project with emphasis on the last ~15 commits: arena mode (squad leaders, NTM, structured fighter inputs, sin/cos headings, arena pause screen) and fighter drill mode (FighterDrillSession, FighterDrillScreen, FighterDrillPauseScreen). Also reviewed: collision system, sensor engine, evolution, snapshot I/O, net viewer, variant rendering, genome management.
>
> **In-scope projects:** NeuroFlyer (primary), plus awareness of neuralnet/neuralnet-ui/evolve library interfaces.
>
> **Total findings:** 100 across 8 categories

## Quick Start

1. Read this summary for the overview
2. Review the sprint plans:
   - [fix-sprint-plan.md](fix-sprint-plan.md) — **34 items** that are objectively wrong (2 CRITICAL data-loss bugs, 1 CRITICAL crash risk, 3 HIGH crashers)
   - [cleanup-sprint-plan.md](cleanup-sprint-plan.md) — **56 items** of technical debt and documentation gaps
   - [new-feature-suggestions.md](new-feature-suggestions.md) — **10 ideas** for performance and features
3. Tell Claude "execute the fix sprint" to start with critical fixes
4. Dive into individual reports below for full details on any finding

## Top Findings

### 1. STALE-001 (CRITICAL): Fighter drill variants lose evolved activations on save
`FighterDrillPauseScreen` skips the per-node activation sync loop that both the scroller and arena save paths include. Saved drill variants silently revert to default activation functions. **Data loss bug.** [Full report →](07-stale-code.md)

### 2. STALE-002 (CRITICAL): NTM snapshots labeled as Solo instead of NTM
Arena pause screen saves NTM companion snapshots with `NetType::Solo`. They display with wrong labels in the net viewer. The `NetType` enum needs an `NTM = 3` value. [Full report →](07-stale-code.md)

### 3. BUG-001 (CRITICAL) + BUG-002/003 (HIGH): Crash risks in arena and drill
- BUG-001: Out-of-bounds access in ArenaSession when bullet `owner_index` is -1 (wraps to `SIZE_MAX`)
- BUG-002: Population/session size mismatch if drill population size is changed via pause screen, causing OOB net access
- BUG-003: `drill_ship_teams_` never resized after evolution, corrupting sensor friend/enemy classification

[Full report →](01-bugs.md)

### 4. ARCH-001/002 (CRITICAL): Engine/UI boundary violations
- `sensor_engine.cpp` (engine layer) imports `theme.h` (UI layer), violating the documented engine/UI separation
- `FighterDrillScreen` contains ~100 lines of game logic (tick loop, sensor queries, net forward passes) that should live in the engine layer

[Full report →](04-architecture.md)

### 5. DOC-001 through DOC-006 (CRITICAL): Massive documentation gaps
Fighter drill mode is entirely undocumented (6 new files, novel phase-based scoring system). Arena mode's CLAUDE.md section is still missing. `collision.h` description is stale (lists 2 functions, actual has 8). The documentation report includes draft text for all gaps.

[Full report →](03-documentation.md)

## Category Summaries

### Bugs
11 findings (1 critical, 3 high, 4 medium, 3 low). The critical crash is an OOB access from negative bullet owner_index casting to size_t. The high-severity bugs are population/session size mismatches in the fighter drill screen and a division-by-zero risk in arena sensor normalization. Medium findings include inconsistent distance normalization, missing bullet boundary checks, and a per-frame network copy in the net viewer. [Full report →](01-bugs.md)

### Assumptions
10 findings (2 high, 5 medium, 3 low; 6 verified clean). Key issues: `convert_variant_to_fighter` hardcodes sensor column semantics without shared constants, and `ArenaGameScreen` hardcodes exactly 2 teams. Snapshot v7 serialization round-trip was verified correct. The compute_arena_input_size/build_arena_ship_input contract was verified consistent. [Full report →](02-assumptions.md)

### Documentation Gaps
23 findings (5 critical, 6 high, 7 medium, 5 low). Fighter drill mode is completely undocumented. Arena mode section still missing from CLAUDE.md. Multiple file tree entries stale. Draft text provided for all gaps. [Full report →](03-documentation.md)

### Architecture
13 findings (2 critical, 3 high, 5 medium, 3 low). The two critical items are engine/UI boundary violations. High items include unnecessary population copies and duplicated session mechanics. Medium items cover inline code duplication, return-by-value in hot paths, and boilerplate construction. [Full report →](04-architecture.md)

### Consolidation
12 findings (3 high, 5 medium, 4 low). Three significant duplication clusters: SDL drawing primitives (130 lines across 2 files), session tick mechanics (~200 lines across 2 files), and ellipse overlap math (80+ lines across 2 files). Also: 5 magic number patterns, 18+ repeated ImGui flag combinations, and 5 duplicated render color constants. [Full report →](05-consolidation.md)

### Dead Code
8 findings (1 critical, 2 high, 3 medium, 2 low). `ArenaConfig::world_diagonal()` exists but is never called while inline sqrt computations persist. `render_variant_net()` is a public API function that no one calls. Six ArenaConfig topology fields are declared but never read. Legacy `cast_rays()` functions are only tested but never called from production. [Full report →](06-dead-code.md)

### Stale Code
9 findings (2 critical, 3 high, 2 medium, 2 low). The two critical items are data-loss bugs: missing activation sync in drill variant saving, and wrong NetType for NTM snapshots. High items include missing death penalty in drill scoring, drill variants saved to wrong directory, and stale ArenaConfig fields. [Full report →](07-stale-code.md)

### Improvements
20 suggestions (4 high, 10 medium, 6 low). The high-impact items are spatial culling for sensor queries (potential 80-90% reduction in intersection tests at high speed), pre-allocated input vectors, and crossover for team evolution. Feature ideas include arena fitness weight UI and a match replay system. [Full report →](08-improvements.md)

## Cross-Cutting Observations

**The fighter drill was a fast copy-paste from arena mode.** This is the single biggest pattern across findings. The drill screen, session, and pause screen all copied code from their arena counterparts but missed several integration points (activation sync, bullet-ship collision, squad directory saving, bullet boundary handling). The duplication also means bug fixes now need to be applied in two places.

**Documentation has not kept pace with features.** Arena mode and fighter drill mode together represent the majority of recent development, but neither has a CLAUDE.md section. The file tree, screen flow, scoring table, controls, and implementation table are all stale. The documentation report provides extensive draft text to close these gaps.

**The engine/UI boundary is starting to erode.** Two violations were found: `sensor_engine.cpp` importing `theme.h`, and `FighterDrillScreen` containing tick-loop logic. These should be addressed before the pattern spreads further.

**World wrapping is inconsistently handled.** Ships wrap but bullets don't. Sensors don't detect across wrap boundaries. `compute_dir_range` doesn't account for wrapping. With the default large arena world (81920x51200), these are minor. But fighter drill's smaller 4000x4000 world makes them significant.
