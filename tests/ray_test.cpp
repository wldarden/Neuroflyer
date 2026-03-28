#include <neuroflyer/ray.h>
#include <neuroflyer/game.h>

#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(RayTest, RayHitsNearbyTower) {
    nf::Triangle tri(400.0f, 500.0f);
    std::vector<nf::Tower> towers = {
        {.x = 400.0f, .y = 350.0f, .radius = 40.0f, .alive = true},
    };
    std::vector<nf::Token> tokens;

    auto rays = nf::cast_rays(tri, towers, tokens, 300.0f, 12);

    float min_dist = 1.0f;
    nf::HitType min_hit = nf::HitType::Nothing;
    for (const auto& r : rays) {
        if (r.distance < min_dist) {
            min_dist = r.distance;
            min_hit = r.hit;
        }
    }
    EXPECT_LT(min_dist, 1.0f);
    EXPECT_EQ(min_hit, nf::HitType::Tower);
}

TEST(RayTest, NoTowersAllClear) {
    nf::Triangle tri(400.0f, 500.0f);
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;

    auto rays = nf::cast_rays(tri, towers, tokens, 300.0f, 12);

    EXPECT_EQ(rays.size(), 12);
    for (const auto& r : rays) {
        EXPECT_FLOAT_EQ(r.distance, 1.0f);
        EXPECT_EQ(r.hit, nf::HitType::Nothing);
    }
}

TEST(RayTest, DeadTowersIgnored) {
    nf::Triangle tri(400.0f, 500.0f);
    std::vector<nf::Tower> towers = {
        {.x = 400.0f, .y = 300.0f, .radius = 20.0f, .alive = false},
    };
    std::vector<nf::Token> tokens;

    auto rays = nf::cast_rays(tri, towers, tokens, 300.0f, 12);

    for (const auto& r : rays) {
        EXPECT_FLOAT_EQ(r.distance, 1.0f);
    }
}

TEST(RayTest, CorrectRayCount) {
    nf::Triangle tri(400.0f, 500.0f);
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;

    auto rays = nf::cast_rays(tri, towers, tokens, 300.0f, 12);
    EXPECT_EQ(rays.size(), 12);

    auto rays8 = nf::cast_rays(tri, towers, tokens, 300.0f, 8);
    EXPECT_EQ(rays8.size(), 8);
}

TEST(RayTest, RayDetectsTokenType) {
    nf::Triangle tri(400.0f, 500.0f);
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens = {
        {.x = 400.0f, .y = 400.0f, .radius = 30.0f, .alive = true},  // close, large
    };

    auto rays = nf::cast_rays(tri, towers, tokens, 300.0f, 13);

    bool found_token = false;
    for (const auto& r : rays) {
        if (r.hit == nf::HitType::Token) {
            found_token = true;
            EXPECT_LT(r.distance, 1.0f);
        }
    }
    EXPECT_TRUE(found_token);
}

TEST(RayTest, TowerOccludesToken) {
    nf::Triangle tri(400.0f, 500.0f);
    // Tower in front of token (closer)
    std::vector<nf::Tower> towers = {
        {.x = 400.0f, .y = 420.0f, .radius = 30.0f, .alive = true},
    };
    std::vector<nf::Token> tokens = {
        {.x = 400.0f, .y = 300.0f, .radius = 20.0f, .alive = true},
    };

    auto rays = nf::cast_rays(tri, towers, tokens, 300.0f, 13);

    // Center ray should hit tower (closer), not token
    auto center = rays[6];  // center of 13 rays
    EXPECT_EQ(center.hit, nf::HitType::Tower);
}
