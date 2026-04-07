#include <neuroflyer/skirmish.h>
#include <neuroflyer/team_evolution.h>
#include <gtest/gtest.h>

#include <cmath>
#include <random>

namespace nf = neuroflyer;

namespace {

nf::ShipDesign make_test_design() {
    nf::ShipDesign d;
    d.memory_slots = 2;
    return d;
}

nf::TeamIndividual make_test_team(std::mt19937& rng, const nf::ShipDesign& design) {
    nf::NtmNetConfig ntm_cfg;
    nf::SquadLeaderNetConfig leader_cfg;
    return nf::TeamIndividual::create(design, {4}, ntm_cfg, leader_cfg, rng);
}

} // namespace

// ---------------------------------------------------------------------------
// SkirmishConfigTest
// ---------------------------------------------------------------------------

class SkirmishConfigTest : public ::testing::Test {
protected:
    nf::SkirmishConfig config;
};

TEST_F(SkirmishConfigTest, Defaults) {
    EXPECT_EQ(config.population_size, 20u);
    EXPECT_EQ(config.seeds_per_match, 3u);
    EXPECT_FLOAT_EQ(config.kill_points, 100.0f);
    EXPECT_FLOAT_EQ(config.death_points, 100.0f);
    EXPECT_EQ(config.world.fighters_per_squad, 8u);
    EXPECT_EQ(config.time_limit_ticks, 3600u);
}

TEST_F(SkirmishConfigTest, BaseKillPointsFormula) {
    config.kill_points = 100.0f;
    config.world.fighters_per_squad = 8;
    config.world.num_squads = 1;
    EXPECT_FLOAT_EQ(config.base_kill_points(), 800.0f);

    config.world.num_squads = 2;
    EXPECT_FLOAT_EQ(config.base_kill_points(), 1600.0f);
}

TEST_F(SkirmishConfigTest, WorldConfigDefaults) {
    config.world.world_width = 2000.0f;
    config.world.world_height = 2000.0f;
    config.world.fighters_per_squad = 4;
    config.world.tower_count = 10;

    // Build ArenaConfig the same way callers do
    nf::ArenaConfig ac;
    ac.world = config.world;
    ac.world.num_teams = 2;
    ac.time_limit_ticks = config.time_limit_ticks;
    ac.sector_size = config.sector_size;
    ac.ntm_sector_radius = config.ntm_sector_radius;
    ac.rounds_per_generation = 1;

    EXPECT_FLOAT_EQ(ac.world.world_width, 2000.0f);
    EXPECT_FLOAT_EQ(ac.world.world_height, 2000.0f);
    EXPECT_EQ(ac.world.fighters_per_squad, 4u);
    EXPECT_EQ(ac.world.tower_count, 10u);
    EXPECT_EQ(ac.world.num_teams, 2u);
    EXPECT_FLOAT_EQ(ac.world.base_hp, config.world.base_hp);
    EXPECT_FLOAT_EQ(ac.world.base_radius, config.world.base_radius);
    EXPECT_FLOAT_EQ(ac.world.rotation_speed, config.world.rotation_speed);
    EXPECT_FLOAT_EQ(ac.world.bullet_max_range, config.world.bullet_max_range);
    EXPECT_EQ(ac.world.wrap_ns, config.world.wrap_ns);
    EXPECT_EQ(ac.world.wrap_ew, config.world.wrap_ew);
    EXPECT_FLOAT_EQ(ac.sector_size, config.sector_size);
    EXPECT_EQ(ac.ntm_sector_radius, config.ntm_sector_radius);
    EXPECT_EQ(ac.rounds_per_generation, 1u);
}

// ---------------------------------------------------------------------------
// SkirmishMatchTest
// ---------------------------------------------------------------------------

class SkirmishMatchTest : public ::testing::Test {
protected:
    std::mt19937 rng{42};
    nf::ShipDesign design = make_test_design();
};

TEST_F(SkirmishMatchTest, RunsToCompletion) {
    nf::SkirmishConfig cfg;
    cfg.world.world_width = 800.0f;
    cfg.world.world_height = 800.0f;
    cfg.world.fighters_per_squad = 2;
    cfg.world.tower_count = 0;
    cfg.world.token_count = 0;
    cfg.time_limit_ticks = 100;

    std::vector<nf::TeamIndividual> teams;
    teams.push_back(make_test_team(rng, design));
    teams.push_back(make_test_team(rng, design));

    auto result = nf::run_skirmish_match(cfg, design, teams, 123);

    EXPECT_TRUE(result.completed);
    EXPECT_GT(result.ticks_elapsed, 0u);
    EXPECT_EQ(result.team_scores.size(), 2u);
}

TEST_F(SkirmishMatchTest, EndsOnTimeLimit) {
    nf::SkirmishConfig cfg;
    cfg.world.world_width = 1000.0f;
    cfg.world.world_height = 1000.0f;
    cfg.world.fighters_per_squad = 2;
    cfg.world.tower_count = 0;
    cfg.world.token_count = 0;
    cfg.world.base_hp = 999999.0f;
    cfg.time_limit_ticks = 50;

    std::vector<nf::TeamIndividual> teams;
    teams.push_back(make_test_team(rng, design));
    teams.push_back(make_test_team(rng, design));

    auto result = nf::run_skirmish_match(cfg, design, teams, 456);

    EXPECT_TRUE(result.completed);
    // Match should end at or very near the time limit
    EXPECT_GE(result.ticks_elapsed, 49u);
    EXPECT_LE(result.ticks_elapsed, 51u);
}

TEST_F(SkirmishMatchTest, ScoresAreFinite) {
    nf::SkirmishConfig cfg;
    cfg.world.world_width = 800.0f;
    cfg.world.world_height = 800.0f;
    cfg.world.fighters_per_squad = 4;
    cfg.world.tower_count = 0;
    cfg.world.token_count = 0;
    cfg.time_limit_ticks = 300;

    std::vector<nf::TeamIndividual> teams;
    teams.push_back(make_test_team(rng, design));
    teams.push_back(make_test_team(rng, design));

    auto result = nf::run_skirmish_match(cfg, design, teams, 789);

    EXPECT_TRUE(result.completed);
    for (std::size_t i = 0; i < result.team_scores.size(); ++i) {
        EXPECT_TRUE(std::isfinite(result.team_scores[i]))
            << "team_scores[" << i << "] = " << result.team_scores[i];
    }
}
