#include <neuroflyer/config.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace nf = neuroflyer;

namespace {
std::string temp_path() {
    return std::filesystem::temp_directory_path() / "neuroflyer_test_config.json";
}
} // namespace

TEST(ConfigTest, DefaultValues) {
    nf::GameConfig cfg;
    EXPECT_FLOAT_EQ(cfg.pts_per_distance, 0.1f);
    EXPECT_FLOAT_EQ(cfg.pts_per_tower, 50.0f);
    EXPECT_FLOAT_EQ(cfg.pts_per_token, 500.0f);
    EXPECT_FLOAT_EQ(cfg.pts_per_bullet, -30.0f);
    EXPECT_EQ(cfg.population_size, 100u);
    EXPECT_EQ(cfg.elitism_count, 10u);
    EXPECT_FLOAT_EQ(cfg.ship_speed, 2.0f);
    EXPECT_EQ(cfg.fire_cooldown, 30);
    EXPECT_EQ(cfg.ship_type, 0);
    EXPECT_EQ(cfg.starting_difficulty, 0);
    EXPECT_FLOAT_EQ(cfg.scroll_speed, 2.0f);
    EXPECT_TRUE(cfg.active_genome.empty());
    EXPECT_EQ(cfg.autosave_interval, 10);
    EXPECT_EQ(cfg.mrca_memory_limit_mb, 64);
    EXPECT_EQ(cfg.mrca_prune_interval, 20);
    EXPECT_FLOAT_EQ(cfg.x_center_mult, 2.0f);
    EXPECT_FLOAT_EQ(cfg.x_edge_mult, 0.0f);
    EXPECT_FLOAT_EQ(cfg.y_top_mult, 2.0f);
    EXPECT_FLOAT_EQ(cfg.y_center_mult, 2.0f);
    EXPECT_FLOAT_EQ(cfg.y_bottom_mult, 0.0f);
}

TEST(ConfigTest, SaveLoadRoundTrip) {
    auto path = temp_path();

    nf::GameConfig original;
    original.pts_per_distance = 0.5f;
    original.pts_per_tower = 100.0f;
    original.pts_per_token = 1000.0f;
    original.pts_per_bullet = -50.0f;
    original.population_size = 200;
    original.elitism_count = 20;
    original.ship_speed = 3.5f;
    original.fire_cooldown = 15;
    original.ship_type = 7;
    original.starting_difficulty = 5;
    original.scroll_speed = 4.0f;
    original.active_genome = "my-best-fleet";
    original.autosave_interval = 5;
    original.mrca_memory_limit_mb = 128;
    original.mrca_prune_interval = 50;
    original.x_center_mult = 3.0f;
    original.x_edge_mult = -1.5f;
    original.y_top_mult = -2.0f;
    original.y_center_mult = 1.0f;
    original.y_bottom_mult = -1.0f;

    original.save(path);
    auto loaded = nf::GameConfig::load(path);

    EXPECT_FLOAT_EQ(loaded.pts_per_distance, 0.5f);
    EXPECT_FLOAT_EQ(loaded.pts_per_tower, 100.0f);
    EXPECT_FLOAT_EQ(loaded.pts_per_token, 1000.0f);
    EXPECT_FLOAT_EQ(loaded.pts_per_bullet, -50.0f);
    EXPECT_EQ(loaded.population_size, 200u);
    EXPECT_EQ(loaded.elitism_count, 20u);
    EXPECT_FLOAT_EQ(loaded.ship_speed, 3.5f);
    EXPECT_EQ(loaded.fire_cooldown, 15);
    EXPECT_EQ(loaded.ship_type, 7);
    EXPECT_EQ(loaded.starting_difficulty, 5);
    EXPECT_FLOAT_EQ(loaded.scroll_speed, 4.0f);
    EXPECT_EQ(loaded.active_genome, "my-best-fleet");
    EXPECT_EQ(loaded.autosave_interval, 5);
    EXPECT_EQ(loaded.mrca_memory_limit_mb, 128);
    EXPECT_EQ(loaded.mrca_prune_interval, 50);
    EXPECT_FLOAT_EQ(loaded.x_center_mult, 3.0f);
    EXPECT_FLOAT_EQ(loaded.x_edge_mult, -1.5f);
    EXPECT_FLOAT_EQ(loaded.y_top_mult, -2.0f);
    EXPECT_FLOAT_EQ(loaded.y_center_mult, 1.0f);
    EXPECT_FLOAT_EQ(loaded.y_bottom_mult, -1.0f);

    std::remove(path.c_str());
}

TEST(ConfigTest, LoadMissingFileReturnsDefaults) {
    auto loaded = nf::GameConfig::load("/nonexistent/path/config.json");
    nf::GameConfig defaults;
    EXPECT_FLOAT_EQ(loaded.pts_per_distance, defaults.pts_per_distance);
    EXPECT_EQ(loaded.ship_type, defaults.ship_type);
}
