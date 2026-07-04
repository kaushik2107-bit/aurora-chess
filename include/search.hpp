#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "board.hpp"
#include "evaluation.hpp"
#include "ttable.hpp"

namespace aurora::chess
{

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
    };

    struct SearchLimits
    {
        std::size_t depth{1};
        std::size_t quiescence_depth{8};
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
