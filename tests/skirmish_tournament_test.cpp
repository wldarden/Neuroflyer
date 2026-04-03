#include <neuroflyer/skirmish_tournament.h>
#include <neuroflyer/team_evolution.h>
#include <gtest/gtest.h>

#include <random>

namespace nf = neuroflyer;

namespace {

nf::ShipDesign make_test_design() {
    nf::ShipDesign d;
    d.memory_slots = 2;
    return d;
}

std::vector<nf::TeamIndividual> make_test_variants(
    std::size_t count, const nf::ShipDesign& design, std::mt19937& rng) {
    nf::NtmNetConfig ntm_cfg;
    nf::SquadLeaderNetConfig leader_cfg;
    std::vector<nf::TeamIndividual> variants;
    variants.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        variants.push_back(nf::TeamIndividual::create(
            design, {4}, ntm_cfg, leader_cfg, rng));
    }
    return variants;
}

nf::SkirmishConfig small_config() {
    nf::SkirmishConfig config;
    config.world_width = 800.0f;
    config.world_height = 800.0f;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 60;  // Very short matches
    config.seeds_per_match = 1;
    return config;
}

constexpr int kMaxSteps = 200000;

} // namespace

// ---------------------------------------------------------------------------
// SkirmishTournamentTest
// ---------------------------------------------------------------------------

class SkirmishTournamentTest : public ::testing::Test {
protected:
    std::mt19937 rng{42};
    nf::ShipDesign design = make_test_design();
};

TEST_F(SkirmishTournamentTest, CompletesWithFourVariants) {
    auto variants = make_test_variants(4, design, rng);
    auto config = small_config();
    nf::SkirmishTournament tournament(config, design, variants, 100);

    int steps = 0;
    while (!tournament.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(tournament.is_complete());
    EXPECT_EQ(tournament.variant_scores().size(), 4u);
    EXPECT_LT(steps, kMaxSteps) << "Tournament did not complete within step limit";
}

TEST_F(SkirmishTournamentTest, CompletesWithSixVariants) {
    auto variants = make_test_variants(6, design, rng);
    auto config = small_config();
    nf::SkirmishTournament tournament(config, design, variants, 200);

    int steps = 0;
    while (!tournament.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(tournament.is_complete());
    EXPECT_EQ(tournament.variant_scores().size(), 6u);
    EXPECT_LT(steps, kMaxSteps) << "Tournament did not complete within step limit";
}

TEST_F(SkirmishTournamentTest, CompletesWithOddCount) {
    auto variants = make_test_variants(5, design, rng);
    auto config = small_config();
    nf::SkirmishTournament tournament(config, design, variants, 300);

    int steps = 0;
    while (!tournament.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(tournament.is_complete());
    EXPECT_EQ(tournament.variant_scores().size(), 5u);
    EXPECT_LT(steps, kMaxSteps) << "Tournament did not complete within step limit";
}

TEST_F(SkirmishTournamentTest, ScoresAccumulate) {
    auto variants = make_test_variants(4, design, rng);
    auto config = small_config();
    config.seeds_per_match = 2;
    nf::SkirmishTournament tournament(config, design, variants, 400);

    int steps = 0;
    while (!tournament.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(tournament.is_complete());

    const auto& scores = tournament.variant_scores();
    bool any_nonzero = false;
    for (float s : scores) {
        if (s != 0.0f) {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero) << "Expected at least one nonzero score after tournament";
}

TEST_F(SkirmishTournamentTest, ArenaAccessibleDuringMatch) {
    auto variants = make_test_variants(2, design, rng);
    auto config = small_config();
    nf::SkirmishTournament tournament(config, design, variants, 500);

    // First step starts the match (arena created)
    ASSERT_FALSE(tournament.step());
    // Second step runs a tick — arena should be live
    ASSERT_FALSE(tournament.step());
    EXPECT_NE(tournament.current_arena(), nullptr);
}

TEST_F(SkirmishTournamentTest, TwoVariantsTournament) {
    auto variants = make_test_variants(2, design, rng);
    auto config = small_config();
    nf::SkirmishTournament tournament(config, design, variants, 600);

    int steps = 0;
    while (!tournament.step() && steps < kMaxSteps) {
        ++steps;
    }

    EXPECT_TRUE(tournament.is_complete());
    EXPECT_EQ(tournament.variant_scores().size(), 2u);
    EXPECT_LT(steps, kMaxSteps) << "Tournament did not complete within step limit";
}
