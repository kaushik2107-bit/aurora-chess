#include "evaluation.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(EvaluationTests, EvaluatesInitialPositionFromSideToMove)
    {
        const aurora::chess::Board board;
        const aurora::chess::PsqtEvaluator evaluator;

        EXPECT_EQ(evaluator.evaluate(board), 10);
    }

    TEST(EvaluationTests, RewardsMaterialAdvantage)
    {
        const aurora::chess::Board board{"4k3/8/8/8/8/8/8/4KQ2 w - - 0 1"};
        const aurora::chess::PsqtEvaluator evaluator;

        EXPECT_GT(evaluator.evaluate(board), 800);
    }

} // namespace
