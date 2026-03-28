# Hangar UI Redesign — Design Spec

**Date:** 2026-03-24
**Status:** Draft
**Scope:** `neuroflyer/src/main.cpp` — Hangar, Create Genome, and Genome Page screens

## Problem

The current Hangar screen uses a basic combo-box file picker for genomes and a variant sub-screen with flat button layout. The Create Genome flow is embedded as a section within the Hangar. There's no visual preview of genomes or neural nets in the selection flow.

## Design

### Hangar Screen (Genome Browser)

**Layout:** Left list panel + right preview panel.

**Left panel — Genome list:**
- Large rows, one per genome. Each row displays:
  - Small body preview (ship sprite thumbnail)
  - Genome name (bold)
  - Variant count
  - Last updated date
- Hovering a row shows that genome's neural net in the right panel preview
- Clicking a row navigates to the **Genome Page** for that genome
- **Top row is always "Create New Genome"** — clicking it navigates to the **Create Genome Page**
- Rows sorted by last updated (most recent first), with "Create New Genome" pinned at top

**Right panel — Neural net preview:**
- Shows the neural net visualization for the currently hovered genome
- If nothing is hovered, shows the selected/last-hovered genome's net
- Uses the existing neural net rendering code (the same renderer used in the net viewer and test bench)
- Shows the genome.bin's network (root genome, not any particular variant)

### Create Genome Page

**Layout:** Full page with 3 zones.

**Top center:** Name input field. Large sci-fi styled text. This is the genome's name.

**Left pane — Configuration controls:**

Three sections in order:

1. **Frame**
   - Ship body selector (the existing ship type 0-9 dropdown/selector)
   - Vision type selector (Raycast / Occulus)

2. **Node Types**
   - Sight rays count (slider, 0-13)
   - Sensor rays count (slider, 0-13)
   - Memory nodes count (slider, 0-16)

3. **Network Layers**
   - Layer count (slider, 1-4)
   - Per-layer node count (one slider per layer, 1-64)

Computed sizes shown: "Inputs: N  Outputs: N"

"Create" button at the bottom.

**Right pane — Live neural net preview:**
- Displays the neural network that would be created with the current settings
- **Responds in real-time** to changing any input: adding/removing layers, changing node counts, changing sensor/memory counts
- The net preview regenerates whenever inputs change (build a temporary network from current settings, render it)
- Same renderer as the net viewer

### Genome Page (Variant Browser)

**What stays:**
- Variant list with name, generation, parent
- All action buttons: Train Fresh, Train from Variant, View Net, Test Bench, Promote, Delete, Lineage Tree
- Back button

**What changes:**
- **No ship preview** on this page (ship body is shown in the Hangar list already)
- Focus is on the variant list and actions

## Implementation Notes

### Neural Net Preview Rendering

The existing net viewer renders to a portion of the SDL window using direct SDL calls (not ImGui). The preview on the Hangar and Create Genome pages needs to render a neural net in the right panel.

Options:
1. **Reuse the existing renderer** — set the render viewport to the right panel's screen rect and call the same drawing code. This requires knowing the screen position of the ImGui panel.
2. **Render to texture** — render the net to an SDL texture, then display it as an ImGui image. More complex but cleaner separation.

Option 1 is simpler and matches the existing pattern (the test bench already renders SDL content alongside ImGui).

### Live Preview on Create Genome Page

When settings change, build a temporary `neuralnet::NetworkTopology` from the current slider values, construct a `neuralnet::Network` with zero weights, and render it. The network doesn't need real weights for visualization — just the topology (layer sizes) matters for the net diagram.

### Ship Body Thumbnail

The ship sprites are already loaded by the renderer. For the Hangar list rows, render a small (32x32 or 48x48) version of the ship sprite. This can be done by rendering to a small SDL texture or by using ImGui's image rendering with the ship sprite texture.

## Non-Goals

- Changing the net viewer or test bench screens
- Changing the pause screen
- Changing game mechanics
- Adding new sensor types (that's the input encoding sub-project)
