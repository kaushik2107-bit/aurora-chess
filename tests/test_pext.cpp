#include "pext.hpp"

#include <gtest/gtest.h>

namespace
{

    class SlidingAttackTests : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            aurora::chess::SlidingAttacks::init();
        }
    };

    TEST_F(SlidingAttackTests, BishopAttacksFromCenterSquare)
    {
        const auto attacks = aurora::chess::SlidingAttacks::bishop_attacks(aurora::chess::Square::D4, 0);
        EXPECT_EQ(aurora::chess::popcount(attacks), 13u);
    }

    TEST_F(SlidingAttackTests, BishopAttacksStopAtOccupancy)
    {
        const auto attacks = aurora::chess::SlidingAttacks::bishop_attacks(aurora::chess::Square::D4, aurora::chess::bit(aurora::chess::Square::E5));
        EXPECT_EQ(aurora::chess::popcount(attacks), 10u);
    }

    TEST_F(SlidingAttackTests, RookAttacksStopAtOccupancy)
    {
        const auto attacks = aurora::chess::SlidingAttacks::rook_attacks(aurora::chess::Square::D4, aurora::chess::bit(aurora::chess::Square::D6));

        EXPECT_NE(attacks & aurora::chess::bit(aurora::chess::Square::D6), 0u);
        EXPECT_EQ(attacks & aurora::chess::bit(aurora::chess::Square::D7), 0u);
    }

} // namespace
