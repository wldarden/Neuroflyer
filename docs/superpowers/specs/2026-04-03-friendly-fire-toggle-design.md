# Friendly Fire Toggle

**Date:** 2026-04-03
**Status:** Approved

## Problem

In skirmish mode, fighters frequently die from friendly fire. The `ally_kills` vector is tracked but never factored into skirmish scoring, so there is zero evolutionary pressure against shooting teammates. Fighters that spray bullets in all directions get rewarded for enemy hits with no cost for fratricide.

## Solution

Make friendly bullets pass through teammate ships (same behavior already used for friendly bases). Add a configurable `friendly_fire` toggle defaulting to off.

## Design

### Config Changes

**`ArenaConfig`** — add `bool friendly_fire = false;`. Read by `ArenaSession` during collision resolution.

**`SkirmishConfig`** — add `bool friendly_fire = false;`. Propagated into the `ArenaConfig` built by `run_skirmish_match()`.

### Collision Logic

In `ArenaSession::resolve_bullet_ship_collisions()`, after the existing self-hit skip (`if (b.owner_index == i) continue`), add a team check:

```
if (!config_.friendly_fire && b.owner_index >= 0) {
    int shooter_team = team_assignments_[static_cast<std::size_t>(b.owner_index)];
    if (shooter_team == team_assignments_[i]) continue;  // bullet passes through teammate
}
```

Guard on `b.owner_index >= 0` since unowned bullets (owner_index == -1) should still hit anyone.

When `friendly_fire` is false (the default), bullets from same-team ships skip collision entirely — they pass through teammates harmlessly.

### UI

Add a "Friendly Fire" checkbox (default unchecked) to **SkirmishConfigScreen** using `ui::checkbox()`. If ArenaConfigScreen has a similar config panel, add it there too.

### Unchanged

- `ally_kills_` tracking remains (will be 0 when FF is off, still useful for stats when FF is on).
- The existing -1000 penalty in `ArenaSession::get_scores()` remains — only triggers when FF is on and an ally kill occurs.
- Fighter drill and attack run are unaffected (single-team modes, no ship-vs-ship collisions).

## Scope

~15 lines of logic across 4 files. No new files, no architectural changes.

| File | Change |
|------|--------|
| `include/neuroflyer/arena_config.h` | Add `bool friendly_fire = false;` |
| `include/neuroflyer/skirmish.h` | Add `bool friendly_fire = false;` |
| `src/engine/arena_session.cpp` | Team check in `resolve_bullet_ship_collisions()` |
| `src/engine/skirmish.cpp` | Propagate `friendly_fire` from SkirmishConfig to ArenaConfig |
| `src/ui/screens/game/skirmish_config_screen.cpp` | Checkbox for friendly fire toggle |
