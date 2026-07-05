#include "engine.hpp"
#include "nnue.hpp"

#include <cmath>

#include <gtest/gtest.h>

namespace
{

    [[nodiscard]] bool load_test_network(aurora::chess::NnueEvaluator& evaluator)
    {
        return evaluator.load("data/network-20220625.nnue") || evaluator.load("../data/network-20220625.nnue") ||
               evaluator.load("../../data/network-20220625.nnue");
    }

    TEST(NnueTests, LoadsNetworkFile)
    {
        aurora::chess::NnueEvaluator evaluator;

        ASSERT_TRUE(load_test_network(evaluator));
        EXPECT_TRUE(evaluator.is_loaded());
        EXPECT_FALSE(evaluator.path().empty());
    }

    TEST(NnueTests, EvaluatesDeterministically)
    {
        const aurora::chess::Board board;
        aurora::chess::NnueEvaluator evaluator;

        ASSERT_TRUE(load_test_network(evaluator));
        const auto first = evaluator.evaluate(board);
        const auto second = evaluator.evaluate(board);

        EXPECT_EQ(first, second);
        EXPECT_LT(std::abs(first), 10000);
    }

    TEST(NnueTests, EvaluatesFromSideToMovePerspective)
    {
        const aurora::chess::Board white{"4k3/8/8/8/8/8/8/4KQ2 w - - 0 1"};
        const aurora::chess::Board black{"4k3/8/8/8/8/8/8/4KQ2 b - - 0 1"};
        aurora::chess::NnueEvaluator evaluator;

        ASSERT_TRUE(load_test_network(evaluator));

        EXPECT_GT(evaluator.evaluate(white), 0);
        EXPECT_LT(evaluator.evaluate(black), 0);
    }

    TEST(NnueTests, IncrementalAccumulatorMatchesFullRefresh)
    {
        aurora::chess::Board board;
        aurora::chess::NnueEvaluator evaluator;

        ASSERT_TRUE(load_test_network(evaluator));

        aurora::chess::NnueEvaluator::Accumulator root;
        evaluator.refresh_accumulator(board, root);
        EXPECT_EQ(evaluator.evaluate(board), evaluator.evaluate(board, root));

        ASSERT_TRUE(board.make_move(aurora::chess::make_move(aurora::chess::Square::E2, aurora::chess::Square::E4,
                                                             aurora::chess::MoveFlag::DoublePawnPush)));

        aurora::chess::NnueEvaluator::Accumulator updated;
        evaluator.update_accumulator(root, board, board.last_dirty_piece(), updated);

        aurora::chess::NnueEvaluator::Accumulator refreshed;
        evaluator.refresh_accumulator(board, refreshed);

        EXPECT_EQ(evaluator.evaluate(board, refreshed), evaluator.evaluate(board, updated));
        EXPECT_EQ(evaluator.evaluate(board), evaluator.evaluate(board, updated));
    }

    TEST(NnueTests, IncrementalAccumulatorHandlesCapturesAndKingRefresh)
    {
        aurora::chess::NnueEvaluator evaluator;
        ASSERT_TRUE(load_test_network(evaluator));

        aurora::chess::Board capture_board;
        aurora::chess::NnueEvaluator::Accumulator accumulator;
        evaluator.refresh_accumulator(capture_board, accumulator);

        ASSERT_TRUE(capture_board.make_move(aurora::chess::make_move(
            aurora::chess::Square::E2, aurora::chess::Square::E4, aurora::chess::MoveFlag::DoublePawnPush)));
        aurora::chess::NnueEvaluator::Accumulator after_e4;
        evaluator.update_accumulator(accumulator, capture_board, capture_board.last_dirty_piece(), after_e4);

        ASSERT_TRUE(capture_board.make_move(aurora::chess::make_move(
            aurora::chess::Square::D7, aurora::chess::Square::D5, aurora::chess::MoveFlag::DoublePawnPush)));
        aurora::chess::NnueEvaluator::Accumulator after_d5;
        evaluator.update_accumulator(after_e4, capture_board, capture_board.last_dirty_piece(), after_d5);

        ASSERT_TRUE(capture_board.make_move(aurora::chess::make_move(
            aurora::chess::Square::E4, aurora::chess::Square::D5, aurora::chess::MoveFlag::Capture)));
        aurora::chess::NnueEvaluator::Accumulator after_capture;
        evaluator.update_accumulator(after_d5, capture_board, capture_board.last_dirty_piece(), after_capture);

        aurora::chess::NnueEvaluator::Accumulator capture_refresh;
        evaluator.refresh_accumulator(capture_board, capture_refresh);
        EXPECT_EQ(evaluator.evaluate(capture_board, capture_refresh), evaluator.evaluate(capture_board, after_capture));

        aurora::chess::Board king_board{"4k3/8/8/8/8/8/4K3/8 w - - 0 1"};
        aurora::chess::NnueEvaluator::Accumulator king_root;
        evaluator.refresh_accumulator(king_board, king_root);
        ASSERT_TRUE(
            king_board.make_move(aurora::chess::make_move(aurora::chess::Square::E2, aurora::chess::Square::E3)));

        aurora::chess::NnueEvaluator::Accumulator king_updated;
        evaluator.update_accumulator(king_root, king_board, king_board.last_dirty_piece(), king_updated);
        EXPECT_EQ(evaluator.evaluate(king_board), evaluator.evaluate(king_board, king_updated));
    }

    TEST(NnueTests, EngineLoadsNetworkAtStartup)
    {
        const aurora::chess::Engine engine;

        EXPECT_TRUE(engine.nnue_loaded());
    }

} // namespace
