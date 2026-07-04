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

    TEST(NnueTests, EngineLoadsNetworkAtStartup)
    {
        const aurora::chess::Engine engine;

        EXPECT_TRUE(engine.nnue_loaded());
    }

} // namespace
