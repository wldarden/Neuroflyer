#include <neuroflyer/sector_grid.h>
#include <gtest/gtest.h>

#include <algorithm>

using namespace neuroflyer;

TEST(SectorGrid, ConstructsCorrectDimensions) {
    // 10000x8000 world, 2000px sectors => 5 cols x 4 rows
    SectorGrid grid(10000.0f, 8000.0f, 2000.0f);
    EXPECT_EQ(grid.cols(), 5);
    EXPECT_EQ(grid.rows(), 4);
}

TEST(SectorGrid, SectorOfClampsToBounds) {
    SectorGrid grid(10000.0f, 8000.0f, 2000.0f);

    // Normal case: (3000, 5000) => col=1, row=2
    auto s1 = grid.sector_of(3000.0f, 5000.0f);
    EXPECT_EQ(s1.col, 1);
    EXPECT_EQ(s1.row, 2);

    // Edge case: exactly on boundary (10000, 8000) => clamped to last sector
    auto s2 = grid.sector_of(10000.0f, 8000.0f);
    EXPECT_EQ(s2.col, 4);
    EXPECT_EQ(s2.row, 3);

    // Negative coords clamp to 0
    auto s3 = grid.sector_of(-100.0f, -100.0f);
    EXPECT_EQ(s3.col, 0);
    EXPECT_EQ(s3.row, 0);
}

TEST(SectorGrid, InsertAndRetrieve) {
    SectorGrid grid(10000.0f, 8000.0f, 2000.0f);
    grid.insert(42, 3000.0f, 5000.0f);  // sector (2, 1)
    grid.insert(99, 3500.0f, 5500.0f);  // same sector (2, 1)
    grid.insert(7, 100.0f, 100.0f);     // sector (0, 0)

    // Diamond radius 0 around (2,1) should return just entities in that sector
    auto result = grid.entities_in_diamond({2, 1}, 0);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(std::find(result.begin(), result.end(), 42) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 99) != result.end());
}

TEST(SectorGrid, DiamondRadius2) {
    // 10x10 grid (20000x20000 world, 2000px sectors)
    SectorGrid grid(20000.0f, 20000.0f, 2000.0f);

    // Place entities at known sectors
    // Center sector (5, 5): entity 0
    grid.insert(0, 11000.0f, 11000.0f);
    // Sector (5, 3) — Manhattan dist 2 from (5,5): entity 1
    grid.insert(1, 7000.0f, 11000.0f);
    // Sector (5, 8) — Manhattan dist 3 from (5,5): entity 2 (OUT of range)
    grid.insert(2, 17000.0f, 11000.0f);
    // Sector (3, 5) — Manhattan dist 2 from (5,5): entity 3
    grid.insert(3, 11000.0f, 7000.0f);
    // Sector (4, 4) — Manhattan dist 2 from (5,5): entity 4
    grid.insert(4, 9000.0f, 9000.0f);

    auto result = grid.entities_in_diamond({5, 5}, 2);

    // Should include entities 0, 1, 3, 4 (within Manhattan dist 2)
    // Should NOT include entity 2 (Manhattan dist 3)
    EXPECT_TRUE(std::find(result.begin(), result.end(), 0) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 1) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 3) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 4) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 2) == result.end());
}

TEST(SectorGrid, DiamondClampsAtEdges) {
    SectorGrid grid(10000.0f, 10000.0f, 2000.0f);  // 5x5 grid
    grid.insert(0, 100.0f, 100.0f);      // sector (0, 0)
    grid.insert(1, 3000.0f, 100.0f);     // sector (0, 1)
    grid.insert(2, 100.0f, 3000.0f);     // sector (1, 0)

    // Diamond radius 2 around corner (0,0) — should not crash, should find all 3
    auto result = grid.entities_in_diamond({0, 0}, 2);
    EXPECT_TRUE(std::find(result.begin(), result.end(), 0) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 1) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), 2) != result.end());
}

TEST(SectorGrid, ClearRemovesAll) {
    SectorGrid grid(10000.0f, 10000.0f, 2000.0f);
    grid.insert(0, 100.0f, 100.0f);
    grid.insert(1, 5000.0f, 5000.0f);
    grid.clear();

    auto result = grid.entities_in_diamond({0, 0}, 10);
    EXPECT_TRUE(result.empty());
}
