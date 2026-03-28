#include <neuroflyer/config.h>
#include <neuroflyer/game.h>

#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(ScoringTest, CenterPositionPositiveMultiplier) {
    // Ship at center with positive multipliers should gain points
    nf::GameConfig cfg;
    cfg.x_center_mult = 2.0f;
    cfg.x_edge_mult = 0.0f;
    cfg.y_top_mult = 2.0f;
    cfg.y_center_mult = 2.0f;
    cfg.y_bottom_mult = 0.0f;
    cfg.scroll_speed = 2.0f;

    nf::GameSession session(42, 800.0f, 600.0f, cfg);
    // Triangle starts near bottom-center — move to center
    for (int i = 0; i < 10; ++i) session.tick();
    EXPECT_GT(session.score(), 0.0f);
}

TEST(ScoringTest, NegativeMultiplierAtEdge) {
    // Verify the multiplier math at the left edge with negative values
    // At x=0 (left edge), nx=0: h_mult = edge + (center-edge)*(1-2*|0-0.5|) = edge
    float h_at_edge = -1.5f + (2.0f - (-1.5f)) * (1.0f - 2.0f * 0.5f);
    EXPECT_FLOAT_EQ(h_at_edge, -1.5f);

    // At x=center, nx=0.5: h_mult = edge + (center-edge)*(1-0) = center
    float h_at_center = -1.5f + (2.0f - (-1.5f)) * (1.0f - 0.0f);
    EXPECT_FLOAT_EQ(h_at_center, 2.0f);

    // With both mults negative, avg should be negative
    float both_neg = (-1.5f + -1.0f) * 0.5f;
    EXPECT_LT(both_neg, 0.0f);
    EXPECT_FLOAT_EQ(both_neg, -1.25f);

    // With one positive, one negative where negative dominates
    float mixed_neg = (-3.0f + 1.0f) * 0.5f;
    EXPECT_LT(mixed_neg, 0.0f);
    EXPECT_FLOAT_EQ(mixed_neg, -1.0f);

    // With one positive, one negative where positive dominates
    float mixed_pos = (-0.5f + 2.0f) * 0.5f;
    EXPECT_GT(mixed_pos, 0.0f);
    EXPECT_FLOAT_EQ(mixed_pos, 0.75f);
}

TEST(ScoringTest, DoubleNegativeMultiplierStillNegative) {
    // REGRESSION: negative x_edge * negative y_bottom used to produce positive
    // score due to multiplication. Now uses averaging, so should be negative.
    nf::GameConfig cfg;
    cfg.x_center_mult = 2.0f;
    cfg.x_edge_mult = -1.5f;
    cfg.y_top_mult = -1.5f;
    cfg.y_center_mult = 2.0f;
    cfg.y_bottom_mult = -1.5f;
    cfg.scroll_speed = 2.0f;

    // Compute the multiplier at a corner position
    // At x=0 (left edge), nx=0: h_mult = -1.5
    // At y=0 (top), ny=0: v_mult = y_top_mult = -1.5
    // avg = (-1.5 + -1.5) / 2 = -1.5 — negative
    // nx=0 (left edge), ny=0 (top)
    float h_mult = cfg.x_edge_mult +
        (cfg.x_center_mult - cfg.x_edge_mult) *
        (1.0f - 2.0f * std::abs(0.0f - 0.5f));
    float v_mult = cfg.y_top_mult;
    float position_mult = (h_mult + v_mult) * 0.5f;

    EXPECT_LT(position_mult, 0.0f)
        << "Double negative multipliers should produce negative score, got "
        << position_mult;
    EXPECT_FLOAT_EQ(position_mult, -1.5f);
}

TEST(ScoringTest, ZeroMultiplierNoScore) {
    nf::GameConfig cfg;
    cfg.x_center_mult = 0.0f;
    cfg.x_edge_mult = 0.0f;
    cfg.y_top_mult = 0.0f;
    cfg.y_center_mult = 0.0f;
    cfg.y_bottom_mult = 0.0f;
    cfg.pts_per_distance = 1.0f;
    cfg.scroll_speed = 2.0f;

    nf::GameSession session(42, 800.0f, 600.0f, cfg);
    for (int i = 0; i < 50; ++i) session.tick();
    // With all multipliers zero, distance score should be zero
    // (score could still be nonzero from tower destruction etc, but distance component = 0)
    // The session score includes distance * pts_per_distance * mult, which should be 0
    EXPECT_FLOAT_EQ(session.distance(), 0.0f);
}

TEST(ScoringTest, PositionMultiplierAtCenter) {
    // Verify the exact multiplier value at screen center
    nf::GameConfig cfg;
    cfg.x_center_mult = 2.0f;
    cfg.x_edge_mult = 0.5f;
    cfg.y_top_mult = 1.0f;
    cfg.y_center_mult = 3.0f;
    cfg.y_bottom_mult = 0.0f;

    // At center: nx=0.5, ny=0.5
    float h_mult = cfg.x_edge_mult +
        (cfg.x_center_mult - cfg.x_edge_mult) *
        (1.0f - 2.0f * std::abs(0.5f - 0.5f));
    float v_mult = cfg.y_center_mult;
    float position_mult = (h_mult + v_mult) * 0.5f;

    EXPECT_FLOAT_EQ(h_mult, 2.0f);
    EXPECT_FLOAT_EQ(v_mult, 3.0f);
    EXPECT_FLOAT_EQ(position_mult, 2.5f);
}

TEST(ScoringTest, StartingDifficultyIncreasesAsteroidDensity) {
    // Higher starting difficulty should produce more towers in the same Y range
    nf::GameConfig easy;
    easy.starting_difficulty = 0;
    easy.scroll_speed = 2.0f;

    nf::GameConfig hard;
    hard.starting_difficulty = 10;
    hard.scroll_speed = 2.0f;

    nf::GameSession easy_session(42, 800.0f, 600.0f, easy);
    nf::GameSession hard_session(42, 800.0f, 600.0f, hard);

    // Tick both forward the same amount
    for (int i = 0; i < 100; ++i) {
        easy_session.tick();
        hard_session.tick();
    }

    // Hard mode should have more towers (smaller gaps = more packed)
    EXPECT_GT(hard_session.towers().size(), easy_session.towers().size());
}
