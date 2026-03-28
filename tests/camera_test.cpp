#include <neuroflyer/camera.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(CameraTest, WorldToScreenCenter) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 1.0f;
    auto [sx, sy] = cam.world_to_screen(500.0f, 500.0f, 800, 600);
    EXPECT_FLOAT_EQ(sx, 400.0f);
    EXPECT_FLOAT_EQ(sy, 300.0f);
}

TEST(CameraTest, WorldToScreenOffset) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 1.0f;
    auto [sx, sy] = cam.world_to_screen(600.0f, 500.0f, 800, 600);
    EXPECT_FLOAT_EQ(sx, 500.0f);
    EXPECT_FLOAT_EQ(sy, 300.0f);
}

TEST(CameraTest, WorldToScreenZoomed) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 2.0f;
    auto [sx, sy] = cam.world_to_screen(550.0f, 500.0f, 800, 600);
    EXPECT_FLOAT_EQ(sx, 500.0f);
    EXPECT_FLOAT_EQ(sy, 300.0f);
}

TEST(CameraTest, ClampToWorldBounds) {
    nf::Camera cam;
    cam.x = 10.0f;
    cam.y = 10.0f;
    cam.zoom = 1.0f;
    cam.clamp_to_world(1000.0f, 1000.0f, 800, 600);
    EXPECT_GE(cam.x, 400.0f);
    EXPECT_GE(cam.y, 300.0f);
}

TEST(CameraTest, ScreenToWorld) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 1.0f;
    auto [wx, wy] = cam.screen_to_world(400.0f, 300.0f, 800, 600);
    EXPECT_FLOAT_EQ(wx, 500.0f);
    EXPECT_FLOAT_EQ(wy, 500.0f);
}
