# Solo vs Squad Fighter Badges & Filter

**Date:** 2026-03-31
**Status:** Design approved

## Problem

The Fighters tab in the Variant Viewer displays all fighter-class variants in a flat list with no visual distinction between original scroller variants (`NetType::Solo`) and arena-converted variants (`NetType::Fighter`). Users cannot tell at a glance which variants have been upgraded for squad-based arena play.

## Solution

Add type badges and an optional filter to the Fighters tab in the Variant Viewer screen.

## Design

### Type Badge

Each variant row in the Fighters tab gets a small colored tag before the variant name, based on `SnapshotHeader::net_type`:

| NetType | Badge | Color |
|---------|-------|-------|
| `Solo` | `[SOLO]` | Cyan `#00d2d3` / `ImVec4(0.0f, 0.82f, 0.83f, 1.0f)` |
| `Fighter` | `[SQUAD]` | Purple `#a29bfe` / `ImVec4(0.64f, 0.61f, 0.99f, 1.0f)` |

The badge renders as `ImGui::TextColored(...)` followed by `ImGui::SameLine()` then the selectable variant name. No new column — it shares the existing "Name" column.

Variants that haven't been through `convert_variant_to_fighter()` naturally show `[SOLO]` since `NetType` defaults to `Solo`. Converted variants already have `NetType::Fighter` set by the drill screen.

### Filter Toggle

A row of three small toggle buttons above the variant table, below the tab bar:

**Buttons:** `All` | `Solo` | `Squad`

- Default: `All` (shows everything)
- `Solo`: filters to `NetType::Solo` only
- `Squad`: filters to `NetType::Fighter` only
- Active button is highlighted, others are dimmed
- Uses `ui::button()` with appropriate styling

**State:** A `FilterMode` enum (`All`, `SoloOnly`, `SquadOnly`) stored as a member variable on `VariantViewerScreen`. Resets to `All` when switching genomes.

**Filtering:** Applied during the table row loop — skip rows that don't match the active filter. The selected variant index maps to the filtered list, not the full `vs_.variants` vector, so selection tracks correctly after filtering. When changing filters, if the currently selected variant is no longer visible, reset selection to the first visible row (or -1 if the filtered list is empty).

### Files Modified

All changes are contained in two files:

- `include/neuroflyer/ui/screens/variant_viewer_screen.h` — add `FilterMode` enum and member variable
- `src/ui/screens/hangar/variant_viewer_screen.cpp` — badge rendering, filter buttons, filtered row loop with index mapping

### Non-Goals

- No changes to the Squad Nets tab (already directory-separated)
- No changes to directory structure or file storage
- No guardrails or mode restrictions (Solo variants can still launch into drill mode; conversion works transparently)
- No changes to the fighter pairing modal
- No changes to how `NetType` is set or saved — we only read the existing snapshot header field
