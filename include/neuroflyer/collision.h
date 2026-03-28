#pragma once

#include <neuroflyer/game.h>   // Triangle (SIZE, vertex positions)

#include <cmath>

namespace neuroflyer {

/// Analytical ray-circle intersection.
/// Returns parameter t >= 0 for the first hit, or -1 if no hit.
/// origin (ox,oy), direction (dx,dy), circle center (cx,cy), radius r.
[[nodiscard]] inline float ray_circle_intersect(
    float ox, float oy, float dx, float dy,
    float cx, float cy, float r) {
    float fx = ox - cx;
    float fy = oy - cy;
    float a = dx * dx + dy * dy;
    float b = 2.0f * (fx * dx + fy * dy);
    float c = fx * fx + fy * fy - r * r;
    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) return -1.0f;

    float sqrt_disc = std::sqrt(discriminant);
    float t1 = (-b - sqrt_disc) / (2.0f * a);
    float t2 = (-b + sqrt_disc) / (2.0f * a);

    if (t1 >= 0.0f) return t1;
    if (t2 >= 0.0f) return t2;
    return -1.0f;
}

/// Point-in-circle test.
[[nodiscard]] inline bool point_in_circle(
    float px, float py, float cx, float cy, float r) {
    float dx = px - cx;
    float dy = py - cy;
    return (dx * dx + dy * dy) < (r * r);
}

/// Check if a bullet point hits a triangle (using the triangle's 3 vertices).
/// Each vertex is surrounded by a small hit circle (HIT_R = SIZE * 0.8).
[[nodiscard]] inline bool bullet_triangle_collision(
    float bx, float by, const Triangle& tri) {
    constexpr float HIT_R = Triangle::SIZE * 0.8f;
    float s = Triangle::SIZE;
    // Same vertex positions as check_collision: top, bottom-left, bottom-right
    return point_in_circle(bx, by, tri.x, tri.y - s, HIT_R) ||
           point_in_circle(bx, by, tri.x - s, tri.y + s, HIT_R) ||
           point_in_circle(bx, by, tri.x + s, tri.y + s, HIT_R);
}

/// Check if any triangle vertex is inside a circle.
[[nodiscard]] inline bool triangle_circle_collision(
    const Triangle& tri, float cx, float cy, float r) {
    float s = Triangle::SIZE;
    // Same vertex positions as check_collision: top, bottom-left, bottom-right
    return point_in_circle(tri.x, tri.y - s, cx, cy, r) ||
           point_in_circle(tri.x - s, tri.y + s, cx, cy, r) ||
           point_in_circle(tri.x + s, tri.y + s, cx, cy, r);
}

/// Check if a bullet point is inside a circle (tower/token).
[[nodiscard]] inline bool bullet_circle_collision(
    float bx, float by, float cx, float cy, float r) {
    return point_in_circle(bx, by, cx, cy, r);
}

} // namespace neuroflyer
