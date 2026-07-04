#include "speed.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(SpeedTests, ReportsInitialPositionStats)
    {
        const aurora::chess::Board board{aurora::chess::kStartFen};
        const auto stats = aurora::chess::speed_stats(board, 4);

        ASSERT_EQ(stats.size(), 4u);

        EXPECT_EQ(stats[0].depth, 1u);
        EXPECT_EQ(stats[0].nodes, 20u);
        EXPECT_EQ(stats[0].captures, 0u);
        EXPECT_EQ(stats[0].en_passant, 0u);
        EXPECT_EQ(stats[0].castles, 0u);
        EXPECT_EQ(stats[0].promotions, 0u);
        EXPECT_EQ(stats[0].checks, 0u);
        EXPECT_EQ(stats[0].checkmates, 0u);

        EXPECT_EQ(stats[2].depth, 3u);
        EXPECT_EQ(stats[2].nodes, 8902u);
        EXPECT_EQ(stats[2].captures, 34u);
        EXPECT_EQ(stats[2].en_passant, 0u);
        EXPECT_EQ(stats[2].castles, 0u);
        EXPECT_EQ(stats[2].promotions, 0u);
        EXPECT_EQ(stats[2].checks, 12u);
        EXPECT_EQ(stats[2].checkmates, 0u);

        EXPECT_EQ(stats[3].depth, 4u);
        EXPECT_EQ(stats[3].nodes, 197281u);
        EXPECT_EQ(stats[3].captures, 1576u);
        EXPECT_EQ(stats[3].en_passant, 0u);
        EXPECT_EQ(stats[3].castles, 0u);
        EXPECT_EQ(stats[3].promotions, 0u);
        EXPECT_EQ(stats[3].checks, 469u);
        EXPECT_EQ(stats[3].checkmates, 8u);
    }

} // namespace
