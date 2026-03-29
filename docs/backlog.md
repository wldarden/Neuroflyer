# NeuroFlyer Backlog

## 1. MRCA History on Pause Screen

Add a button to the pause screen that shows a lineage tree of the last completed generation's elite tree. The lineage tree has the sim's parent variant as the root node, with every elite individual from the last completed generation as descendants.

## 2. Lineage Tree Viewer

A reusable, project-independent lineage tree visualization component. Should live in the evolve library or as an optional add-on so it can be used by NeuroFlyer (pause screen, hangar), AntSim, and future projects.

- **A.** Page/component that displays a provided lineage tree
- **B.** Variants (individuals) shown as nodes
- **C.** Connections indicate ancestry links
- **D.** Double-click a node to open a sub-page displaying the variant's neural network
- **E.** Evolutionary distance metric: calculate distance between nodes by processing their neural network parameters. Optionally color-code the tree by this distance — distant individuals differ more in color than nearby ones
- **F.** Multiple colorization modes:
  - Color by evolutionary distance
  - Color by sensor composition
  - Color by node count
  - Color by layer count
  - (brainstorm and add more as appropriate)
- **G.** Actions on nodes: "Create Genome from Node", "Create Variant from Node", "Delete Node", etc.
- **H.** Click a node to view stats about the variant it represents
- **I.** Must be independent/reusable — not tied to NeuroFlyer. Designed as an evolve package optional feature or similar so it can be dropped into any project with an evolution pipeline

## 3. Evolution Settings Management

Expose evolution parameters in the UI:
- Enable/disable mutation methods (e.g. crossover on/off)
- Tournament size
- Mutation rate
- Mutation jump size
- Other NEAT-specific parameters as applicable

## 4. Manual Variant Recombination

- Select 2 variants and sexually reproduce them (crossover)
- Or select 1 variant and asexually reproduce it (mutation only)
- After child is created, explore its neural network and parameters
- Choose to save the child or discard it

## 5. Grpahics Settings

Main menu setting to configure graphics settings of the game

## 6. Main Menu "Settings" Page

Page for managing game settings (e.g., resolution, audio, controls). Accessible from the main menu. 
Main Settings should apply to global things. If a setting is specific to a particular variant or sim run, it should be 
managed in the hangar or pause screen instead. 
- Main Settings page should have Category sections for different types of settings (Graphics, Audio, Controls, etc.) 
- Clean UI for adjusting settings.
- Add "Settings" option to main menu list (fly, hangar, quit)

## 7. MRCA Tracker Assertion with Small Populations

`record_generation` in `mrca_tracker.cpp:48` asserts `elite_ids.size() == elite_count_`, but `record_mrca` in `fly_session.cpp` clamps to `min(elitism_count, population.size())`. When the population is smaller than the configured elitism count, the assertion fires. Fix: either clamp `elite_count_` on construction or have `record_generation` handle a shorter elite list.

## 8. Arena Perception Phase 2: Commander Net + Multiple Squads

Add the commander net layer on top of the working squad broadcast system (Phase 1). One recurrent commander net per team sees macro state (4x4 sector grid, squad stats, top-threat analysis) and outputs state signals fed to all squad broadcast nets. Multiple squads per team, each getting different broadcasts. Validates that the commander layer adds strategic value over bare squad nets. See `docs/superpowers/specs/2026-03-28-arena-perception-design.md` Phase 2 section.

## 9. Arena Perception Phase 3: Enemy Analysis Net + Multi-Team Matches

Add the enemy analysis sub-net (shared weights, duplicated per enemy team). Outputs threat_score for top-1 selection + mode signals (defend_home, attack_base, attack_squad) interpreted by the game engine. Multi-team matches with small-group matchmaking (groups of 3-4). Full team-level evolution with the complete four-net architecture. See `docs/superpowers/specs/2026-03-28-arena-perception-design.md` Phase 3 section.

## 10. Arena Matchmaking Improvements

Current Phase 1 arena always pits team genomes 0 vs 1. Implement proper matchmaking: round-robin or random pairing across the full population so all team genomes get match experience each generation. Consider Swiss-style pairing (similar-fitness teams matched together) for better selection pressure.
