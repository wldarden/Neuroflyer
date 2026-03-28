# Game Engine Architecture — Future Plan

**Status:** Planned, not yet implemented
**Date:** 2026-03-26

## Goal

Replace the current bespoke entity structs (Tower, Token, Bullet, Triangle) with a composable entity system that scales to:

- Different tower types (shooting, health levels, destructible)
- Enemies that move, shoot, have health, drop power-ups
- Arena mode where ships battle each other
- Power-ups (weapon upgrades, health, shields, speed boosts)
- Secondary weapons (bombs, lasers, airstrikes with cooldowns)
- Boss fights with patterns and phases

## Approach

Composition-based (ECS-lite). Entity is a lightweight core (position, alive, type). Behaviors come from composable components:

- `Collider` — radius, hitbox shape
- `Health` — current/max HP, damage handling
- `Movement` — velocity, speed, movement patterns
- `Weapon` — fire rate, cooldown, projectile type
- `AI` — behavior patterns (patrol, chase, boss phases)
- `Effect` — power-up effects, buffs, shields

The collision system operates on anything with a `Collider` — doesn't need to know entity types. Adding a boss = composing existing components with new config, not writing new collision/spawn/render functions.

## Current State

Flat structs in `game.h`: Tower, Token, Bullet, Triangle. Separate vectors per type, free collision functions, hardcoded spawn logic. Works for level 1 but every new entity type requires touching multiple files.

## Prerequisite

UI architecture should be designed first — it has more immediate pain points and a larger backlog of blocked changes.
