/// @file test_math.cpp
/// @brief Smoke tests for vector_math types as used by campello_audio.
///
/// campello_audio does not define its own math types — it uses
/// systems::leal::vector_math directly. These tests verify that the
/// dependency is correctly wired and that the types behave as expected
/// within campello_audio's use patterns (3D positions, velocities, directions).

#include <gtest/gtest.h>
#include <vector_math/vector_math.hpp>

using Vec3 = systems::leal::vector_math::Vector3<float>;

// ---------------------------------------------------------------------------
// Basic construction
// ---------------------------------------------------------------------------

TEST(Vec3, DefaultConstruct) {
    Vec3 v;
    EXPECT_FLOAT_EQ(v.data[0], 0.0f);
    EXPECT_FLOAT_EQ(v.data[1], 0.0f);
    EXPECT_FLOAT_EQ(v.data[2], 0.0f);
}

TEST(Vec3, ComponentConstruct) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.data[0], 1.0f);
    EXPECT_FLOAT_EQ(v.data[1], 2.0f);
    EXPECT_FLOAT_EQ(v.data[2], 3.0f);
}

// ---------------------------------------------------------------------------
// Arithmetic (needed for 3D attenuation / Doppler calculations)
// ---------------------------------------------------------------------------

TEST(Vec3, Addition) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    Vec3 c = a + b;
    EXPECT_FLOAT_EQ(c.data[0], 5.0f);
    EXPECT_FLOAT_EQ(c.data[1], 7.0f);
    EXPECT_FLOAT_EQ(c.data[2], 9.0f);
}

TEST(Vec3, Subtraction) {
    Vec3 a(5.0f, 5.0f, 5.0f);
    Vec3 b(2.0f, 3.0f, 1.0f);
    Vec3 c = a - b;
    EXPECT_FLOAT_EQ(c.data[0], 3.0f);
    EXPECT_FLOAT_EQ(c.data[1], 2.0f);
    EXPECT_FLOAT_EQ(c.data[2], 4.0f);
}

TEST(Vec3, ScalarMultiply) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    Vec3 r = v * 2.0f;
    EXPECT_FLOAT_EQ(r.data[0], 2.0f);
    EXPECT_FLOAT_EQ(r.data[1], 4.0f);
    EXPECT_FLOAT_EQ(r.data[2], 6.0f);
}

// ---------------------------------------------------------------------------
// Geometric operations (needed for 3D panning / attenuation)
// ---------------------------------------------------------------------------

TEST(Vec3, DotProduct) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(0.0f, 1.0f, 0.0f);
    EXPECT_FLOAT_EQ(a.dot(b), 0.0f);
    EXPECT_FLOAT_EQ(a.dot(a), 1.0f);
}

TEST(Vec3, Length) {
    Vec3 v(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.length(), 5.0f);
}

TEST(Vec3, Normalize) {
    Vec3 v(0.0f, 3.0f, 0.0f);
    auto n = v.normalized();   // returns Vec<float,3> base type
    EXPECT_FLOAT_EQ(n.data[0], 0.0f);
    EXPECT_FLOAT_EQ(n.data[1], 1.0f);
    EXPECT_FLOAT_EQ(n.data[2], 0.0f);
}

TEST(Vec3, Distance) {
    Vec3 a(0.0f, 0.0f, 0.0f);
    Vec3 b(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(a.distanceTo(b), 5.0f);
}
