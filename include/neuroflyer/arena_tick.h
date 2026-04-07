#pragma once

#include <neuroflyer/arena_world.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/squad_leader.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neuroflyer {

/// Run fighter nets with pre-computed (scripted) squad leader inputs.
/// For each alive ship: builds arena sensor input, forwards fighter net, decodes output,
/// applies actions. Then calls world.tick() and returns events.
[[nodiscard]] TickEvents tick_fighters_scripted(
    ArenaWorld& world,
    const ShipDesign& fighter_design,
    std::span<neuralnet::Network> fighter_nets,
    const std::vector<SquadLeaderFighterInputs>& sl_inputs,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams,
    std::vector<std::vector<float>>* out_fighter_inputs = nullptr);

/// Run full NTM -> squad leader -> fighter pipeline with learned nets.
/// Per team: builds sector grid, gathers threats, runs NTM, runs squad leader.
/// Per fighter: computes SL inputs, builds sensor input, forwards fighter net.
/// Then calls world.tick() and returns events.
[[nodiscard]] TickEvents tick_arena_with_leader(
    ArenaWorld& world,
    const ArenaWorldConfig& config,
    const ShipDesign& fighter_design,
    std::span<neuralnet::Network> ntm_nets,
    std::span<neuralnet::Network> leader_nets,
    std::span<neuralnet::Network> fighter_nets,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams,
    uint32_t time_limit_ticks,
    int ntm_sector_radius,
    float sector_size,
    std::vector<SquadLeaderFighterInputs>* out_sl_inputs = nullptr,
    std::vector<std::vector<float>>* out_leader_inputs = nullptr,
    std::vector<std::vector<float>>* out_fighter_inputs = nullptr);

} // namespace neuroflyer
