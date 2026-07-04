#include "magic.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(MagicBitboardTests, BishopAttacksFromCenterSquare)
    {
        const auto attacks = aurora::chess::MagicBitboards::bishop_moves(aurora::chess::Square::D4, 0);
        EXPECT_EQ(aurora::chess::popcount(attacks), 13u);
    }

    TEST(MagicBitboardTests, BishopAttacksStopAtOccupancy)
    {
        const auto attacks = aurora::chess::MagicBitboards::bishop_moves(aurora::chess::Square::D4, aurora::chess::bit(aurora::chess::Square::E5));
        EXPECT_EQ(aurora::chess::popcount(attacks), 10u);
    }

} // namespace
