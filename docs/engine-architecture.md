# Game Engine Architecture

**Status:** Current architecture validated through 3 game modes
**Last updated:** 2026-03-31

## Current State

Flat structs in `game.h`: Tower, Token, Bullet, Triangle. Arena mode added rotation, directional bullets, and bases. Separate vectors per entity type, free collision functions, session classes per game mode.

This approach has been validated through:
- **Scroller mode** — `GameSession` with vertical scrolling, tower/token spawning, position-based scoring
- **Arena mode** — `ArenaSession` with rotation+thrust movement, team bases, squad stats, directional bullets
- **Fighter drill mode** — `FighterDrillSession` with phase-based training, scripted squad inputs, starbase attacks

Each mode has its own session class with dedicated collision resolution, scoring, and entity management. Some physics code is duplicated between ArenaSession and FighterDrillSession (intentional scope decision — see CLAUDE.md Key Design Decisions).

## Possible Future Direction: ECS-lite

A composition-based entity system could reduce duplication and make new entity types easier to add. Entity = lightweight core (position, alive, type). Behaviors from composable components (Collider, Health, Movement, Weapon, AI, Effect).

This is no longer a prerequisite for any planned feature. The flat-struct approach works for the current scope. Consider ECS if:
- Adding many new entity types beyond the current set
- Duplication between session classes becomes a maintenance burden
- Need complex entity interactions (e.g., bosses with multiple phases and health bars)
