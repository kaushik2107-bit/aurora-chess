#include "search.hpp"

#include "movegen.hpp"

#include <atomic>

#include <gtest/gtest.h>

namespace
{

    TEST(SearchTests, FindsALegalMoveFromInitialPosition)
    {
        const aurora::chess::Board board;
        aurora::chess::TranspositionTable table{};
        const auto result = aurora::chess::search(board, aurora::chess::SearchLimits{.depth = 1}, table);
        const auto moves = aurora::chess::MoveGenerator{}.generate(board);

        bool found = false;
        for (const auto& entry : moves)
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
        aurora::chess::TranspositionTable table{};
        const auto result = aurora::chess::search(board, aurora::chess::SearchLimits{.depth = 2}, table);

        EXPECT_EQ(result.best_move, 0);
        EXPECT_LT(result.score, -aurora::chess::kMateScore + 10);
    }

    TEST(SearchTests, RecordsEachIterativeDeepeningStep)
    {
        const aurora::chess::Board board;
        aurora::chess::TranspositionTable table{};
        const auto result = aurora::chess::search(board, aurora::chess::SearchLimits{.depth = 3}, table);

        ASSERT_EQ(result.iterations.size(), 3u);
        EXPECT_EQ(result.iterations[0].depth, 1u);
        EXPECT_EQ(result.iterations[1].depth, 2u);
        EXPECT_EQ(result.iterations[2].depth, 3u);
        EXPECT_EQ(result.depth, 3u);
        EXPECT_GE(result.pv.size(), 3u);
        EXPECT_GE(result.iterations[2].pv.size(), 3u);
        EXPECT_GE(result.selective_depth, result.depth);
    }

    TEST(SearchTests, SupportsLazySmpWorkers)
    {
        const aurora::chess::Board board;
        aurora::chess::TranspositionTable table{};
        std::atomic_size_t reported_iterations{0};
        aurora::chess::SearchLimits limits;
        limits.depth = 2;
        limits.threads = 2;
        limits.on_iteration = [&reported_iterations](const aurora::chess::SearchIteration&)
        { reported_iterations.fetch_add(1, std::memory_order_relaxed); };

        const auto result = aurora::chess::search(board, limits, table);
        const auto moves = aurora::chess::MoveGenerator{}.generate(board);

        bool found = false;
        for (const auto& entry : moves)
        {
            found = found || entry.move == result.best_move;
        }

        EXPECT_TRUE(found);
        EXPECT_EQ(result.depth, 2u);
        EXPECT_GT(result.nodes, 0u);
        EXPECT_EQ(reported_iterations.load(std::memory_order_relaxed), 2u);
    }

    TEST(SearchTests, ObservesStopFlag)
    {
        const aurora::chess::Board board;
        aurora::chess::TranspositionTable table{};
        std::atomic_bool stop{true};
        aurora::chess::SearchLimits limits;
        limits.depth = 6;
        limits.stop = &stop;

        const auto result = aurora::chess::search(board, limits, table);

        EXPECT_EQ(result.depth, 0u);
        EXPECT_EQ(result.best_move, 0);
    }

} // namespace
