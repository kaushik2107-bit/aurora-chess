#include "search.hpp"

#include "movegen.hpp"

#include <atomic>
#include <chrono>

#include <gtest/gtest.h>

namespace
{

    class StopDuringEvaluation final : public aurora::chess::Evaluator
    {
    public:
        explicit StopDuringEvaluation(std::atomic_bool& stop) : stop_(stop) {}

        [[nodiscard]] aurora::chess::Score evaluate(const aurora::chess::Board&) const noexcept override
        {
            if (evaluations_.fetch_add(1, std::memory_order_relaxed) >= 1)
            {
                stop_.store(true, std::memory_order_relaxed);
            }
            return 0;
        }

    private:
        std::atomic_bool& stop_;
        mutable std::atomic_size_t evaluations_{0};
    };

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
        std::atomic_uint64_t last_reported_nodes{0};
        aurora::chess::SearchLimits limits;
        limits.depth = 2;
        limits.threads = 2;
        limits.on_iteration =
            [&reported_iterations, &last_reported_nodes](const aurora::chess::SearchIteration& iteration)
        {
            const auto previous = last_reported_nodes.exchange(iteration.nodes, std::memory_order_relaxed);
            EXPECT_GE(iteration.nodes, previous);
            reported_iterations.fetch_add(1, std::memory_order_relaxed);
        };

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
        EXPECT_GE(result.nodes, last_reported_nodes.load(std::memory_order_relaxed));
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

    TEST(SearchTests, ObservesExpiredDeadline)
    {
        const aurora::chess::Board board;
        aurora::chess::TranspositionTable table{};
        aurora::chess::SearchLimits limits;
        limits.depth = 6;
        limits.deadline = std::chrono::steady_clock::now();

        const auto result = aurora::chess::search(board, limits, table);

        EXPECT_EQ(result.depth, 0u);
        EXPECT_EQ(result.best_move, 0);
    }

    TEST(SearchTests, DoesNotPublishStoppedIteration)
    {
        const aurora::chess::Board board;
        aurora::chess::TranspositionTable table{};
        std::atomic_bool stop{false};
        std::atomic_bool callback_after_stop{false};
        const StopDuringEvaluation evaluator{stop};

        aurora::chess::SearchLimits limits;
        limits.depth = 6;
        limits.stop = &stop;
        limits.on_iteration = [&callback_after_stop, &stop](const aurora::chess::SearchIteration&)
        {
            if (stop.load(std::memory_order_relaxed))
            {
                callback_after_stop.store(true, std::memory_order_relaxed);
            }
        };

        const auto result = aurora::chess::search(board, limits, table, evaluator);

        EXPECT_FALSE(callback_after_stop.load(std::memory_order_relaxed));
        EXPECT_TRUE(result.iterations.empty() || result.depth == result.iterations.back().depth);
    }

} // namespace
