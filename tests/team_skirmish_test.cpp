#include <neuroflyer/team_skirmish.h>
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

nf::TeamPool make_test_pool(
    std::size_t squad_count,
    std::size_t fighters_per_squad,
    const nf::ShipDesign& design,
    std::mt19937& rng) {

    nf::NtmNetConfig ntm_cfg;
    nf::SquadLeaderNetConfig leader_cfg;

    nf::TeamPool pool;
    for (std::size_t s = 0; s < squad_count; ++s) {
        pool.squad_population.push_back(
            nf::TeamIndividual::create(design, {4}, ntm_cfg, leader_cfg, rng));
    }
    pool.squad_scores.resize(squad_count, 0.0f);

    // Fighter population must use arena-sized inputs. Extract fighter individuals
    // from TeamIndividual::create() so they have the correct arena input size.
    for (std::size_t f = 0; f < fighters_per_squad * squad_count; ++f) {
        auto team_ind = nf::TeamIndividual::create(design, {4}, ntm_cfg, leader_cfg, rng);
        pool.fighter_population.push_back(std::move(team_ind.fighter_individual));
    }
    pool.fighter_scores.resize(fighters_per_squad * squad_count, 0.0f);

    return pool;
}

nf::SkirmishConfig small_skirmish_config() {
    nf::SkirmishConfig cfg;
    cfg.world_width = 2000.0f;
    cfg.world_height = 2000.0f;
    cfg.fighters_per_squad = 4;
    cfg.num_squads_per_team = 1;
    cfg.tower_count = 0;
    cfg.token_count = 0;
    cfg.time_limit_ticks = 60;
    cfg.seeds_per_match = 1;
    return cfg;
}

constexpr int kMaxSteps = 500000;

} // namespace

// ---------------------------------------------------------------------------
// ShipAssignmentTest — build_ship_assignments()
// ---------------------------------------------------------------------------

class ShipAssignmentTest : public ::testing::Test {
protected:
    std::mt19937 rng{42};
    nf::ShipDesign design = make_test_design();
};

TEST_F(ShipAssignmentTest, TotalSizeCorrect) {
    // 2 teams, each with 1 squad of 4 fighters
    constexpr std::size_t num_teams = 2;
    constexpr std::size_t squads_per_team = 1;
    constexpr std::size_t fighters_per_squad = 4;

    std::vector<nf::TeamPool> pools;
    for (std::size_t t = 0; t < num_teams; ++t) {
        pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
    }

    const std::vector<std::size_t> match_teams = {0, 1};
    const auto assignments = nf::build_ship_assignments(
        pools, match_teams, squads_per_team, fighters_per_squad);

    EXPECT_EQ(assignments.size(), num_teams * squads_per_team * fighters_per_squad);
}

TEST_F(ShipAssignmentTest, TeamMappingCorrect) {
    // 2 teams, 1 squad each, 4 fighters per squad
    constexpr std::size_t squads_per_team = 1;
    constexpr std::size_t fighters_per_squad = 4;

    std::vector<nf::TeamPool> pools;
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));

    const std::vector<std::size_t> match_teams = {0, 1};
    const auto assignments = nf::build_ship_assignments(
        pools, match_teams, squads_per_team, fighters_per_squad);

    // First 4 ships belong to team 0, next 4 to team 1
    for (std::size_t i = 0; i < fighters_per_squad; ++i) {
        EXPECT_EQ(assignments[i].team_id, 0u) << "ship " << i << " should be team 0";
    }
    for (std::size_t i = fighters_per_squad; i < 2 * fighters_per_squad; ++i) {
        EXPECT_EQ(assignments[i].team_id, 1u) << "ship " << i << " should be team 1";
    }
}

TEST_F(ShipAssignmentTest, FighterIndexInRange) {
    constexpr std::size_t squads_per_team = 1;
    constexpr std::size_t fighters_per_squad = 4;

    std::vector<nf::TeamPool> pools;
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));

    const std::vector<std::size_t> match_teams = {0, 1};
    const auto assignments = nf::build_ship_assignments(
        pools, match_teams, squads_per_team, fighters_per_squad);

    for (std::size_t i = 0; i < assignments.size(); ++i) {
        const auto& a = assignments[i];
        EXPECT_LT(a.squad_index, squads_per_team)
            << "ship " << i << " squad_index out of range";
        EXPECT_LT(a.fighter_index, pools[a.team_id].fighter_population.size())
            << "ship " << i << " fighter_index out of range";
    }
}

TEST_F(ShipAssignmentTest, ThreeTeams) {
    // 3 teams, 1 squad each, 4 fighters per squad → 12 ships total
    constexpr std::size_t num_teams = 3;
    constexpr std::size_t squads_per_team = 1;
    constexpr std::size_t fighters_per_squad = 4;

    std::vector<nf::TeamPool> pools;
    for (std::size_t t = 0; t < num_teams; ++t) {
        pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
    }

    const std::vector<std::size_t> match_teams = {0, 1, 2};
    const auto assignments = nf::build_ship_assignments(
        pools, match_teams, squads_per_team, fighters_per_squad);

    EXPECT_EQ(assignments.size(), num_teams * squads_per_team * fighters_per_squad);

    // Each team block should have the correct team_id
    for (std::size_t t = 0; t < num_teams; ++t) {
        for (std::size_t f = 0; f < fighters_per_squad; ++f) {
            std::size_t idx = t * fighters_per_squad + f;
            EXPECT_EQ(assignments[idx].team_id, t)
                << "ship " << idx << " should be team " << t;
        }
    }
}

// ---------------------------------------------------------------------------
// TeamSkirmishMatchTest — run_team_skirmish_match()
// ---------------------------------------------------------------------------

class TeamSkirmishMatchTest : public ::testing::Test {
protected:
    std::mt19937 rng{99};
    nf::ShipDesign design = make_test_design();
};

TEST_F(TeamSkirmishMatchTest, CompletesAndReturnsCorrectShipCount) {
    constexpr std::size_t squads_per_team = 1;
    constexpr std::size_t fighters_per_squad = 4;

    nf::SkirmishConfig cfg = small_skirmish_config();

    std::vector<nf::TeamPool> pools;
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));

    const std::vector<std::size_t> match_teams = {0, 1};
    const auto assignments = nf::build_ship_assignments(
        pools, match_teams, squads_per_team, fighters_per_squad);

    const auto result = nf::run_team_skirmish_match(cfg, design, pools, assignments, 42u);

    EXPECT_TRUE(result.completed);
    EXPECT_GT(result.ticks_elapsed, 0u);
    EXPECT_EQ(result.ship_scores.size(), assignments.size());
}

TEST_F(TeamSkirmishMatchTest, ShipScoresAreFinite) {
    constexpr std::size_t squads_per_team = 1;
    constexpr std::size_t fighters_per_squad = 4;

    nf::SkirmishConfig cfg = small_skirmish_config();
    cfg.time_limit_ticks = 120;

    std::vector<nf::TeamPool> pools;
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));

    const std::vector<std::size_t> match_teams = {0, 1};
    const auto assignments = nf::build_ship_assignments(
        pools, match_teams, squads_per_team, fighters_per_squad);

    const auto result = nf::run_team_skirmish_match(cfg, design, pools, assignments, 77u);

    ASSERT_TRUE(result.completed);
    for (std::size_t i = 0; i < result.ship_scores.size(); ++i) {
        EXPECT_TRUE(std::isfinite(result.ship_scores[i]))
            << "ship_scores[" << i << "] = " << result.ship_scores[i];
    }
}

TEST_F(TeamSkirmishMatchTest, EndsAtOrBeforeTimeLimit) {
    constexpr std::size_t squads_per_team = 1;
    constexpr std::size_t fighters_per_squad = 4;

    nf::SkirmishConfig cfg = small_skirmish_config();
    cfg.base_hp = 999999.0f;  // prevent early base destruction
    cfg.time_limit_ticks = 80;

    std::vector<nf::TeamPool> pools;
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
    pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));

    const std::vector<std::size_t> match_teams = {0, 1};
    const auto assignments = nf::build_ship_assignments(
        pools, match_teams, squads_per_team, fighters_per_squad);

    const auto result = nf::run_team_skirmish_match(cfg, design, pools, assignments, 55u);

    EXPECT_TRUE(result.completed);
    EXPECT_LE(result.ticks_elapsed, cfg.time_limit_ticks + 1u);
}

// ---------------------------------------------------------------------------
// TeamSkirmishSessionTest — RoundRobin and FreeForAll modes
// ---------------------------------------------------------------------------

class TeamSkirmishSessionTest : public ::testing::Test {
protected:
    std::mt19937 rng{7};
    nf::ShipDesign design = make_test_design();

    nf::TeamSkirmishConfig make_config(nf::CompetitionMode mode) {
        nf::TeamSkirmishConfig cfg;
        cfg.arena = small_skirmish_config();
        cfg.competition_mode = mode;
        return cfg;
    }

    std::vector<nf::TeamPool> make_pools(std::size_t num_teams) {
        constexpr std::size_t squads_per_team = 1;
        constexpr std::size_t fighters_per_squad = 4;
        std::vector<nf::TeamPool> pools;
        pools.reserve(num_teams);
        for (std::size_t t = 0; t < num_teams; ++t) {
            pools.push_back(make_test_pool(squads_per_team, fighters_per_squad, design, rng));
        }
        return pools;
    }
};

TEST_F(TeamSkirmishSessionTest, RoundRobinThreeTeamsCompletes) {
    // 3 teams in RoundRobin → 3 matches (0v1, 0v2, 1v2)
    auto cfg = make_config(nf::CompetitionMode::RoundRobin);
    auto pools = make_pools(3);

    nf::TeamSkirmishSession session(cfg, design, std::move(pools), 42u);

    EXPECT_EQ(session.total_matches(), 3u);

    int steps = 0;
    while (!session.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(session.is_complete());
    EXPECT_LT(steps, kMaxSteps) << "RoundRobin session did not complete within step limit";
}

TEST_F(TeamSkirmishSessionTest, FreeForAllThreeTeamsCompletes) {
    // 3 teams in FreeForAll → 1 match with all 3 teams
    auto cfg = make_config(nf::CompetitionMode::FreeForAll);
    auto pools = make_pools(3);

    nf::TeamSkirmishSession session(cfg, design, std::move(pools), 13u);

    EXPECT_EQ(session.total_matches(), 1u);

    int steps = 0;
    while (!session.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(session.is_complete());
    EXPECT_LT(steps, kMaxSteps) << "FreeForAll session did not complete within step limit";
}

TEST_F(TeamSkirmishSessionTest, RoundRobinTwoTeamsCompletes) {
    // 2 teams in RoundRobin → 1 match (0v1)
    auto cfg = make_config(nf::CompetitionMode::RoundRobin);
    auto pools = make_pools(2);

    nf::TeamSkirmishSession session(cfg, design, std::move(pools), 99u);

    EXPECT_EQ(session.total_matches(), 1u);

    int steps = 0;
    while (!session.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(session.is_complete());
    EXPECT_LT(steps, kMaxSteps) << "Two-team RoundRobin did not complete within step limit";
}

TEST_F(TeamSkirmishSessionTest, ArenaAccessibleDuringMatch) {
    auto cfg = make_config(nf::CompetitionMode::RoundRobin);
    auto pools = make_pools(2);

    nf::TeamSkirmishSession session(cfg, design, std::move(pools), 321u);

    // First step starts the match
    ASSERT_FALSE(session.step());
    // Second step runs a tick — arena should be accessible
    ASSERT_FALSE(session.step());
    EXPECT_NE(session.current_arena(), nullptr);
}

TEST_F(TeamSkirmishSessionTest, ShipAssignmentsPopulated) {
    auto cfg = make_config(nf::CompetitionMode::RoundRobin);
    auto pools = make_pools(2);

    nf::TeamSkirmishSession session(cfg, design, std::move(pools), 11u);

    // Step once to start the first match
    ASSERT_FALSE(session.step());

    const auto& assignments = session.ship_assignments();
    EXPECT_GT(assignments.size(), 0u);
}
