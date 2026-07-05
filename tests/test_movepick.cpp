#include "movepick.hpp"

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace
{

    TEST(MovePickerTests, ReturnsEveryGeneratedMoveOnce)
    {
        const aurora::chess::Board board;
        const auto generated = aurora::chess::MoveGenerator{}.generate(board);
        aurora::chess::MovePicker picker{board};
        std::array<bool, 1u << 16> seen{};
        std::size_t count = 0;

        for (aurora::chess::Move move = picker.next(); move != 0; move = picker.next())
        {
            ASSERT_FALSE(seen[move]);
            seen[move] = true;
            ++count;
        }

        EXPECT_EQ(count, generated.size());
    }

    TEST(MovePickerTests, ReturnsLegalTtMoveFirst)
    {
        const aurora::chess::Board board;
        aurora::chess::MoveOrdering ordering;
        ordering.tt_move = aurora::chess::make_move(aurora::chess::Square::G1, aurora::chess::Square::F3);

        aurora::chess::MovePicker picker{board, ordering};

        EXPECT_EQ(picker.next(), ordering.tt_move);
    }

    TEST(MovePickerTests, IgnoresUnavailableTtMove)
    {
        const aurora::chess::Board board;
        aurora::chess::MoveOrdering ordering;
        ordering.tt_move = aurora::chess::make_move(aurora::chess::Square::A1, aurora::chess::Square::A8);

        aurora::chess::MovePicker picker{board, ordering};

        EXPECT_NE(picker.next(), ordering.tt_move);
    }

    TEST(MovePickerTests, ReturnsGoodNoisyMovesBeforeQuiets)
    {
        const aurora::chess::Board board{"4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1"};
        const auto capture = aurora::chess::make_move(aurora::chess::Square::E4, aurora::chess::Square::D5,
                                                      aurora::chess::MoveFlag::Capture);

        aurora::chess::MovePicker picker{board};

        EXPECT_EQ(picker.next(), capture);
    }

    TEST(MovePickerTests, ReturnsKillerBeforeOrdinaryQuiets)
    {
        const aurora::chess::Board board;
        const auto killer = aurora::chess::make_move(aurora::chess::Square::G1, aurora::chess::Square::F3);
        aurora::chess::MoveOrdering ordering;
        ordering.killers[0] = killer;

        aurora::chess::MovePicker picker{board, ordering};

        EXPECT_EQ(picker.next(), killer);
    }

    TEST(MovePickerTests, ReturnsCounterMoveBeforeOrdinaryQuiets)
    {
        const aurora::chess::Board board;
        const auto counter = aurora::chess::make_move(aurora::chess::Square::B1, aurora::chess::Square::C3);
        aurora::chess::MoveOrdering ordering;
        ordering.counter_move = counter;

        aurora::chess::MovePicker picker{board, ordering};

        EXPECT_EQ(picker.next(), counter);
        EXPECT_NE(picker.next(), counter);
    }

    TEST(MovePickerTests, UsesContinuationHistoryForQuietOrdering)
    {
        const aurora::chess::Board board;
        const auto preferred = aurora::chess::make_move(aurora::chess::Square::B1, aurora::chess::Square::C3);
        const auto previous =
            aurora::chess::piece_square_history_index(aurora::chess::Piece::BlackPawn, aurora::chess::Square::E5);
        const auto current =
            aurora::chess::piece_square_history_index(aurora::chess::Piece::WhiteKnight, aurora::chess::Square::C3);
        std::vector<int> continuation_history(aurora::chess::kPieceSquareHistoryBuckets *
                                              aurora::chess::kPieceSquareHistoryBuckets);
        continuation_history[previous * aurora::chess::kPieceSquareHistoryBuckets + current] = 10'000;

        aurora::chess::MoveOrdering ordering;
        ordering.continuation_history = &continuation_history;
        ordering.previous_piece_square = previous;

        aurora::chess::MovePicker picker{board, ordering};

        EXPECT_EQ(picker.next(), preferred);
    }

    TEST(MovePickerTests, DelaysSeeLosingCapturesUntilAfterQuiets)
    {
        const aurora::chess::Board board{"4r2k/4p3/8/8/8/8/4Q3/7K w - - 0 1"};
        const auto bad_capture = aurora::chess::make_move(aurora::chess::Square::E2, aurora::chess::Square::E7,
                                                          aurora::chess::MoveFlag::Capture);

        aurora::chess::MovePicker picker{board};

        EXPECT_NE(picker.next(), bad_capture);
    }

} // namespace
