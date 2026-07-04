#include "see.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(SeeTests, ScoresUndefendedCapture)
    {
        const aurora::chess::Board board{"4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1"};
        const auto move = aurora::chess::make_move(aurora::chess::Square::E4, aurora::chess::Square::D5,
                                                   aurora::chess::MoveFlag::Capture);

        EXPECT_EQ(aurora::chess::static_exchange_eval(board, move), 100);
        EXPECT_TRUE(aurora::chess::see_ge(board, move, 0));
    }

    TEST(SeeTests, ScoresLosingCapture)
    {
        const aurora::chess::Board board{"4r2k/4p3/8/8/8/8/4Q3/7K w - - 0 1"};
        const auto move = aurora::chess::make_move(aurora::chess::Square::E2, aurora::chess::Square::E7,
                                                   aurora::chess::MoveFlag::Capture);

        EXPECT_EQ(aurora::chess::static_exchange_eval(board, move), -800);
        EXPECT_FALSE(aurora::chess::see_ge(board, move, 0));
    }

    TEST(SeeTests, FoldsEqualPawnTradeToZero)
    {
        const aurora::chess::Board board{"3qk3/8/8/3p4/4P3/8/8/4K3 w - - 0 1"};
        const auto move = aurora::chess::make_move(aurora::chess::Square::E4, aurora::chess::Square::D5,
                                                   aurora::chess::MoveFlag::Capture);

        EXPECT_EQ(aurora::chess::static_exchange_eval(board, move), 0);
        EXPECT_TRUE(aurora::chess::see_ge(board, move, 0));
        EXPECT_FALSE(aurora::chess::see_ge(board, move, 1));
    }

    TEST(SeeTests, ScoresPromotionGain)
    {
        const aurora::chess::Board board{"4k3/P7/8/8/8/8/8/4K3 w - - 0 1"};
        const auto move = aurora::chess::make_move(aurora::chess::Square::A7, aurora::chess::Square::A8,
                                                   aurora::chess::MoveFlag::QueenPromotion);

        EXPECT_EQ(aurora::chess::static_exchange_eval(board, move), 800);
    }

    TEST(SeeTests, ScoresEnPassantAsPawnCapture)
    {
        const aurora::chess::Board board{"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"};
        const auto move = aurora::chess::make_move(aurora::chess::Square::E5, aurora::chess::Square::D6,
                                                   aurora::chess::MoveFlag::EnPassant);

        EXPECT_EQ(aurora::chess::static_exchange_eval(board, move), 100);
    }

} // namespace
