#include <neuroflyer/base.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(BaseTest, Construction) {
    nf::Base b{100.0f, 200.0f, 50.0f, 1000.0f, 0};
    EXPECT_FLOAT_EQ(b.x, 100.0f);
    EXPECT_FLOAT_EQ(b.y, 200.0f);
    EXPECT_FLOAT_EQ(b.radius, 50.0f);
    EXPECT_FLOAT_EQ(b.hp, 1000.0f);
    EXPECT_FLOAT_EQ(b.max_hp, 1000.0f);
    EXPECT_EQ(b.team_id, 0);
    EXPECT_TRUE(b.alive());
}

TEST(BaseTest, TakeDamage) {
    nf::Base b{0, 0, 50.0f, 100.0f, 0};
    b.take_damage(30.0f);
    EXPECT_FLOAT_EQ(b.hp, 70.0f);
    EXPECT_TRUE(b.alive());
    b.take_damage(80.0f);
    EXPECT_FLOAT_EQ(b.hp, 0.0f);
    EXPECT_FALSE(b.alive());
}

TEST(BaseTest, HpNormalized) {
    nf::Base b{0, 0, 50.0f, 200.0f, 0};
    EXPECT_FLOAT_EQ(b.hp_normalized(), 1.0f);
    b.take_damage(100.0f);
    EXPECT_FLOAT_EQ(b.hp_normalized(), 0.5f);
}
