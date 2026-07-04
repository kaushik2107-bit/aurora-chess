#include "search.hpp"

#include "movegen.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(SearchTests, FindsALegalMoveFromInitialPosition)
    {
        const aurora::chess::Board board;
        const auto result = aurora::chess::Searcher{}.search(board, aurora::chess::SearchLimits{.depth = 1});
        const auto moves = aurora::chess::MoveGenerator{}.generate(board);

        bool found = false;
        for (const auto &entry : moves)
        {
            found = found || entry.move == result.best_move;
        }

        EXPECT_TRUE(found);
        EXPECT_EQ(result.depth, 1u);
        EXPECT_GT(result.nodes, 0u);
    }

    TEST(SearchTests, ReportsNoMoveInCheckmate)
    {
        const aurora::chess::Board board{"7k/6Q1/6K1/8/8/8/8/8 b - - 0 1"};
        const auto result = aurora::chess::Searcher{}.search(board, aurora::chess::SearchLimits{.depth = 2});

        EXPECT_EQ(result.best_move, 0);
        EXPECT_LT(result.score, -aurora::chess::kMateScore + 10);
    }

    TEST(SearchTests, RecordsEachIterativeDeepeningStep)
    {
        const aurora::chess::Board board;
        const auto result = aurora::chess::Searcher{}.search(board, aurora::chess::SearchLimits{.depth = 3});

        ASSERT_EQ(result.iterations.size(), 3u);
        EXPECT_EQ(result.iterations[0].depth, 1u);
        EXPECT_EQ(result.iterations[1].depth, 2u);
        EXPECT_EQ(result.iterations[2].depth, 3u);
        EXPECT_EQ(result.depth, 3u);
        EXPECT_GE(result.pv.size(), 3u);
        EXPECT_GE(result.iterations[2].pv.size(), 3u);
        EXPECT_GE(result.selective_depth, result.depth);
    }

} // namespace
