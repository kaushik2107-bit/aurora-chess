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
        std::function<void(const SearchIteration &)> on_iteration{};
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

    class Searcher
    {
    public:
        explicit Searcher(const Evaluator &evaluator = default_evaluator()) noexcept;

        [[nodiscard]] SearchResult search(const Board &board, SearchLimits limits);

    private:
        [[nodiscard]] Score alpha_beta(Board &board, std::size_t depth, Score alpha, Score beta, std::size_t ply,
                                       std::vector<Move> &pv);
        [[nodiscard]] Score quiescence(Board &board, Score alpha, Score beta, std::size_t ply,
                                       std::vector<Move> &pv);

        const Evaluator &evaluator_;
        TranspositionTable table_;
        std::uint64_t nodes_{0};
        std::size_t selective_depth_{0};
        std::size_t max_quiescence_ply_{0};
    };

} // namespace aurora::chess
