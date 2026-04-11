#include <neuroflyer/arena_tick.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/team_evolution.h>

#include <gtest/gtest.h>

#include <random>

namespace nf = neuroflyer;

// ── tick_fighters_scripted ──────────────────────────────────────────────────

class ArenaTickScriptedTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.world_width = 4000.0f;
        config_.world_height = 4000.0f;
        config_.num_teams = 1;
        config_.num_squads = 1;
        config_.fighters_per_squad = 3;
        config_.tower_count = 0;
        config_.token_count = 0;

        world_ = std::make_unique<nf::ArenaWorld>(config_, 42);

        // No sensors, 2 memory slots: input = 0 + 6 + 2 = 8, output = 5 + 2 = 7
        design_.sensors.clear();
        design_.memory_slots = 2;

        const std::size_t input_size = nf::compute_arena_input_size(design_);
        const std::size_t output_size = nf::compute_output_size(design_);

        ASSERT_EQ(input_size, 8u);
        ASSERT_EQ(output_size, 7u);

        std::mt19937 rng(123);
        const std::size_t num_ships = world_->ships().size();
        for (std::size_t i = 0; i < num_ships; ++i) {
            auto ind = nf::Individual::random(input_size, {4}, output_size, rng);
            fighter_nets_.push_back(ind.build_network());
        }

        sl_inputs_.resize(num_ships);  // all zeros (neutral)
        recurrent_states_.resize(num_ships, std::vector<float>(design_.memory_slots, 0.0f));

        // All ships on team 0
        ship_teams_.resize(num_ships, 0);
    }

    nf::ArenaWorldConfig config_;
    std::unique_ptr<nf::ArenaWorld> world_;
    nf::ShipDesign design_;
    std::vector<neuralnet::Network> fighter_nets_;
    std::vector<nf::SquadLeaderFighterInputs> sl_inputs_;
    std::vector<std::vector<float>> recurrent_states_;
    std::vector<int> ship_teams_;
};

TEST_F(ArenaTickScriptedTest, TickAdvances) {
    ASSERT_EQ(world_->current_tick(), 0u);

    auto events = nf::tick_fighters_scripted(
        *world_, design_, fighter_nets_, sl_inputs_,
        recurrent_states_, ship_teams_);

    EXPECT_EQ(world_->current_tick(), 1u);
}

TEST_F(ArenaTickScriptedTest, CapturesFighterInputs) {
    const std::size_t num_ships = world_->ships().size();
    std::vector<std::vector<float>> captured_inputs(num_ships);

    (void)nf::tick_fighters_scripted(
        *world_, design_, fighter_nets_, sl_inputs_,
        recurrent_states_, ship_teams_, &captured_inputs);

    // All ships start alive, so all should have captured inputs
    for (std::size_t i = 0; i < num_ships; ++i) {
        EXPECT_FALSE(captured_inputs[i].empty())
            << "Ship " << i << " should have captured input";
        EXPECT_EQ(captured_inputs[i].size(), nf::compute_arena_input_size(design_));
    }
}

TEST_F(ArenaTickScriptedTest, RecurrentStateCorrectSize) {
    (void)nf::tick_fighters_scripted(
        *world_, design_, fighter_nets_, sl_inputs_,
        recurrent_states_, ship_teams_);

    // Recurrent state vectors should have the correct size (memory_slots)
    for (const auto& rs : recurrent_states_) {
        EXPECT_EQ(rs.size(), design_.memory_slots);
    }
}

// ── tick_arena_with_leader ──────────────────────────────────────────────────

class ArenaTickWithLeaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.world_width = 4000.0f;
        config_.world_height = 4000.0f;
        config_.num_teams = 2;
        config_.num_squads = 1;
        config_.fighters_per_squad = 2;
        config_.tower_count = 0;
        config_.token_count = 0;

        world_ = std::make_unique<nf::ArenaWorld>(config_, 42);

        design_.sensors.clear();
        design_.memory_slots = 2;

        std::mt19937 rng(456);
        const std::vector<std::size_t> fighter_hidden = {4};
        nf::NtmNetConfig ntm_config;
        nf::SquadLeaderNetConfig leader_config;

        for (std::size_t t = 0; t < config_.num_teams; ++t) {
            auto team_ind = nf::TeamIndividual::create(
                design_, fighter_hidden, ntm_config, leader_config, rng);
            ntm_nets_.push_back(team_ind.build_ntm_network());
            leader_nets_.push_back(team_ind.build_squad_network());
            fighter_nets_.push_back(team_ind.build_fighter_network());
        }

        const std::size_t num_ships = world_->ships().size();
        recurrent_states_.resize(num_ships, std::vector<float>(design_.memory_slots, 0.0f));

        ship_teams_.resize(num_ships);
        for (std::size_t i = 0; i < num_ships; ++i) {
            ship_teams_[i] = world_->team_of(i);
        }
    }

    nf::ArenaWorldConfig config_;
    std::unique_ptr<nf::ArenaWorld> world_;
    nf::ShipDesign design_;
    std::vector<neuralnet::Network> ntm_nets_;
    std::vector<neuralnet::Network> leader_nets_;
    std::vector<neuralnet::Network> fighter_nets_;
    std::vector<std::vector<float>> recurrent_states_;
    std::vector<int> ship_teams_;

    static constexpr uint32_t kTimeLimitTicks = 3600;
    static constexpr int kNtmSectorRadius = 2;
    static constexpr float kSectorSize = 2000.0f;
};

TEST_F(ArenaTickWithLeaderTest, TickAdvances) {
    ASSERT_EQ(world_->current_tick(), 0u);

    auto events = nf::tick_arena_with_leader(
        *world_, config_, design_,
        ntm_nets_, leader_nets_, fighter_nets_,
        recurrent_states_, ship_teams_,
        kTimeLimitTicks, kNtmSectorRadius, kSectorSize);

    EXPECT_EQ(world_->current_tick(), 1u);
}

TEST_F(ArenaTickWithLeaderTest, MultipleTicks) {
    for (int t = 0; t < 10; ++t) {
        (void)nf::tick_arena_with_leader(
            *world_, config_, design_,
            ntm_nets_, leader_nets_, fighter_nets_,
            recurrent_states_, ship_teams_,
            kTimeLimitTicks, kNtmSectorRadius, kSectorSize);
    }
    EXPECT_EQ(world_->current_tick(), 10u);
}

TEST_F(ArenaTickWithLeaderTest, CapturesSlInputs) {
    const std::size_t num_ships = world_->ships().size();
    std::vector<nf::SquadLeaderFighterInputs> sl_inputs(num_ships);

    (void)nf::tick_arena_with_leader(
        *world_, config_, design_,
        ntm_nets_, leader_nets_, fighter_nets_,
        recurrent_states_, ship_teams_,
        kTimeLimitTicks, kNtmSectorRadius, kSectorSize,
        &sl_inputs);

    // Verify at least one ship has non-zero SL inputs (squads on different sides
    // of the map should have different target headings)
    bool any_nonzero = false;
    for (const auto& sl : sl_inputs) {
        if (sl.squad_target_heading != 0.0f || sl.squad_target_distance != 0.0f ||
            sl.aggression != 0.0f || sl.spacing != 0.0f) {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero);
}

TEST_F(ArenaTickWithLeaderTest, CapturesLeaderInputs) {
    std::vector<std::vector<float>> leader_inputs(config_.num_teams);

    (void)nf::tick_arena_with_leader(
        *world_, config_, design_,
        ntm_nets_, leader_nets_, fighter_nets_,
        recurrent_states_, ship_teams_,
        kTimeLimitTicks, kNtmSectorRadius, kSectorSize,
        nullptr, &leader_inputs);

    for (std::size_t t = 0; t < config_.num_teams; ++t) {
        EXPECT_FALSE(leader_inputs[t].empty())
            << "Team " << t << " should have captured leader inputs";
    }
}

TEST_F(ArenaTickWithLeaderTest, CapturesFighterInputs) {
    const std::size_t num_ships = world_->ships().size();
    std::vector<std::vector<float>> fighter_inputs(num_ships);

    (void)nf::tick_arena_with_leader(
        *world_, config_, design_,
        ntm_nets_, leader_nets_, fighter_nets_,
        recurrent_states_, ship_teams_,
        kTimeLimitTicks, kNtmSectorRadius, kSectorSize,
        nullptr, nullptr, &fighter_inputs);

    for (std::size_t i = 0; i < num_ships; ++i) {
        EXPECT_FALSE(fighter_inputs[i].empty())
            << "Ship " << i << " should have captured fighter input";
        EXPECT_EQ(fighter_inputs[i].size(), nf::compute_arena_input_size(design_));
    }
}
