# Overnight Review — Mar 30, 01:35 PM

> **Scope:** Full NeuroFlyer repo (neuroflyer + libs/neuralnet, neuralnet-ui, evolve dependencies), with focus on the last 30 commits adding arena mode (squad leaders, NTM, sector grid, net type tabs, squad training)
> **Total findings:** 97 across 8 categories

## Quick Start

1. Read this summary for the overview
2. Review the sprint plans:
   - [fix-sprint-plan.md](fix-sprint-plan.md) — **24 items** that are objectively wrong (3 CRITICAL)
   - [cleanup-sprint-plan.md](cleanup-sprint-plan.md) — **46 items** of technical debt & documentation
   - [new-feature-suggestions.md](new-feature-suggestions.md) — **17 ideas**
3. Tell Claude "execute the fix sprint" to start with critical fixes
4. Dive into individual reports below for full details on any finding

## Top Findings

### 🔴 1. Arena evolution is broken — only 2 of 20 genomes get fitness (BUG-001)
`current_team_indices_` is hardcoded to `{0, 1}`. Only team genomes 0 and 1 are ever evaluated. The other 18 genomes always have fitness 0, collapsing natural selection. **This is the single highest-priority fix.** [Full report →](01-bugs.md)

### 🔴 2. Occulus sensors silently broken in arena mode (STALE-001)
`query_arena_sensor()` treats ALL sensors as raycasts — no `switch(sensor.type)`. Any genome with Occulus (ellipse) sensors produces incorrect detection in arena training. [Full report →](07-stale-code.md)

### 🔴 3. Fighter net viewer shows wrong labels (STALE-002 + ASM-001)
The live net viewer during arena play uses scroller labels (POS X, POS Y, SPEED) instead of arena labels (TGT HDG, TGT DST, CTR HDG, etc.). Label count also mismatches (35 vs 43 for a typical config). [Full report →](07-stale-code.md)

### 🟡 4. Arena has no pause screen — can't save trained nets (STALE-003)
Space bar only toggles a boolean. No config overlay, no save variants tab. Squad training mode is fire-and-forget with no way to extract results. [Full report →](07-stale-code.md)

### 🟡 5. 130-line arena tick loop duplicated in 2 files (STALE-005 + CONS-001/003)
The NTM → squad leader → fighter pipeline is copy-pasted between `arena_game_screen.cpp` and `arena_match.cpp`. Fitness scoring is also duplicated with a subtle denominator drift. [Full report →](07-stale-code.md)

## Category Summaries

### Bugs
11 bugs found. 1 CRITICAL (broken evolution), 3 HIGH (wrong collision shapes, wrong labels, division-by-zero risks), 4 MEDIUM, 3 LOW. The most impactful is BUG-001 (only 2 of 20 team genomes evaluated) which renders arena evolution nearly useless. [Full report →](01-bugs.md)

### Assumptions
22 assumptions verified. 15 confirmed correct (NTM input/output sizes, snapshot serialization, entity ID encoding, enum completeness). 3 confirmed BROKEN (fighter labels/colors, `current_team_indices_` assumes num_teams==2). 4 WARNINGs (safe but fragile patterns). [Full report →](02-assumptions.md)

### Documentation Gaps
16 gaps identified. Arena mode is entirely absent from CLAUDE.md — no mention of ArenaConfig, ArenaSession, squad leaders, NTM, sector grid, or the NetType enum. The snapshot format version is documented as "v1-v4" but the actual version is v7. The file tree is missing ~30 files and lists 3 that no longer exist. Draft text provided for all gaps. [Full report →](03-documentation.md)

### Architecture
10 findings. 2 HIGH: engine file imports UI header (`sensor_engine.cpp` → `theme.h`), and `FlySessionScreen` uses static locals for all state (the largest framework violation). Several MEDIUM issues around encapsulation (public `renderer_`), redundant config structs, and rotation-unaware collisions. [Full report →](04-architecture.md)

### Consolidation
13 opportunities. 3 HIGH: squad leader input computation duplicated in 3 files (all should use `compute_dir_range()`), evolution functions 90% identical (extract with callback), fitness scoring duplicated with subtle drift. Several MEDIUM items around SectorGrid rebuild cost and magic numbers. [Full report →](05-consolidation.md)

### Dead Code
11 items. 2 HIGH: `run_arena_match()` is dead in production (240 lines, only tests call it), and the entire legacy screen system (`go_to_screen`, `Screen` enum, `LegacyScreen`, `sync_legacy_navigation`) is completely dead — migration is finished but bridge code was never removed. [Full report →](06-dead-code.md)

### Stale Code
14 items. 2 CRITICAL: Occulus sensors broken in arena, fighter net viewer labels wrong. 3 HIGH: no arena pause screen, duplicated tick loop, missing conversion assertion. The stale code category had the most impactful findings because new arena features weren't fully integrated into existing systems. [Full report →](07-stale-code.md)

### Improvements
31 suggestions. Top value: cache world_diagonal (trivial), better error messages for input size mismatch (saves hours of debugging), deduplicate the tick loop (prevents drift), and arena fitness charts (essential for understanding training). [Full report →](08-improvements.md)

## Cross-Cutting Observations

**The arena tick loop is the central integration risk.** It exists in two places (interactive + headless), both with identical ~130-line NTM → squad leader → fighter pipelines. Multiple findings (STALE-005, CONS-001, CONS-003, CONS-007, CONS-008, DEAD-001) all trace back to this duplication. Extracting a shared `tick_arena_frame()` function would resolve 6+ findings at once and is the highest-leverage refactor.

**The scroller → arena transition left integration gaps.** Several systems (sensor labels, display ordering, collision shapes, Occulus sensor support) were written for scroller mode and not fully updated for arena mode. A systematic audit of every place that dispatches on `NetType` or `SensorType` would catch the remaining gaps.

**Documentation is significantly behind the code.** CLAUDE.md doesn't mention arena mode at all, despite it being the primary development focus of the last 30 commits. Updating it now prevents future Claude sessions from starting with incomplete context.
