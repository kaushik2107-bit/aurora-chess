#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "board.hpp"
#include "evaluation.hpp"
#include "ttable.hpp"

namespace aurora::chess
{

    class ThreadPool;

    constexpr Score kInfiniteScore = 32000;
    constexpr Score kMateScore = 30000;

    struct SearchIteration
    {
        Move best_move{0};
        std::vector<Move> pv;
        Score score{0};
        std::uint64_t nodes{0};
        std::size_t depth{0};
        std::size_t selective_depth{0};
        bool completed{true};
    };

    struct SearchLimits
    {
        std::size_t depth{1};
        std::size_t quiescence_depth{8};
        std::size_t threads{1};
        std::size_t worker_id{0};
        std::atomic_bool* stop{nullptr};
        std::atomic_uint64_t* shared_nodes{nullptr};
        std::atomic<std::int64_t>* shared_deadline_ms{nullptr};
        ThreadPool* thread_pool{nullptr};
        std::optional<std::chrono::steady_clock::time_point> deadline{};
        std::function<void(const SearchIteration&)> on_iteration{};
    };

    struct SearchResult
    {
        Move best_move{0};
        std::vector<Move> pv;
        Score score{0};
        std::uint64_t nodes{0};
        std::size_t depth{0};
        std::size_t selective_depth{0};
        std::vector<SearchIteration> iterations;
    };

    [[nodiscard]] SearchResult search(const Board& board, SearchLimits limits, TranspositionTable& table,
                                      const Evaluator& evaluator = default_evaluator());

} // namespace aurora::chess
