#include "search.hpp"

#include "movepick.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace aurora::chess
{
    namespace
    {
        struct SearchState;

        void update_pv(std::vector<Move>& pv, Move move, const std::vector<Move>& child_pv)
        {
            pv.clear();
            pv.reserve(child_pv.size() + 1);
            pv.push_back(move);
            pv.insert(pv.end(), child_pv.begin(), child_pv.end());
        }

        struct SearchState
        {
            const Evaluator& evaluator;
            TranspositionTable& table;
            std::uint64_t nodes{0};
            std::size_t selective_depth{0};
            std::size_t max_quiescence_ply{0};
            std::array<std::array<Move, 256>, 2> killers{};
            std::vector<int> history{std::vector<int>(1u << 16)};
            bool enable_check_extension{true};
        };

        [[nodiscard]] MoveOrdering move_ordering(Move tt_move, const SearchState& state, std::size_t ply)
        {
            MoveOrdering ordering;
            ordering.tt_move = tt_move;
            ordering.history = &state.history;

            if (ply < state.killers[0].size())
            {
                ordering.killers = {state.killers[0][ply], state.killers[1][ply]};
            }

            return ordering;
        }

        void record_quiet_cutoff(SearchState& state, Move move, std::size_t depth, std::size_t ply)
        {
            if (is_noisy(move) || ply >= state.killers[0].size())
            {
                return;
            }

            if (state.killers[0][ply] != move)
            {
                state.killers[1][ply] = state.killers[0][ply];
                state.killers[0][ply] = move;
            }

            constexpr int kMaxHistoryScore = 1'000'000;
            const int bonus = static_cast<int>(depth * depth);
            int& score = state.history[static_cast<std::size_t>(move)];
            score = std::min(kMaxHistoryScore, score + bonus);
        }

        struct MoveLoopResult
        {
            bool generated_any{false};
            std::size_t searched{0};
        };

        template <typename MoveFilter, typename MoveHandler>
        MoveLoopResult visit_ordered_moves(Board& board, MoveOrdering ordering, MoveFilter&& filter,
                                           MoveHandler&& handler)
        {
            MovePicker picker{board, ordering};
            if (!picker.generated_any())
            {
                return {};
            }

            MoveLoopResult result{true, 0};
            for (Move move = picker.next(); move != 0; move = picker.next())
            {
                if (!filter(move))
                {
                    continue;
                }

                if (!board.make_move(move))
                {
                    continue;
                }

                const bool stop = handler(move, result.searched);
                ++result.searched;
                board.undo_move();

                if (stop)
                {
                    break;
                }
            }

            return result;
        }

        class SearchWorker
        {
        public:
            SearchWorker(SearchLimits limits, TranspositionTable& table, const Evaluator& evaluator)
                : limits_(std::move(limits)), state_{evaluator, table}
            {
            }

            [[nodiscard]] SearchResult search(const Board& board);

        private:
            [[nodiscard]] SearchIteration search_root(Board& board, std::size_t depth, Move previous_best);
            [[nodiscard]] Score alpha_beta(Board& board, std::size_t depth, Score alpha, Score beta, std::size_t ply,
                                           std::vector<Move>& pv);
            [[nodiscard]] Score quiescence(Board& board, Score alpha, Score beta, std::size_t ply,
                                           std::vector<Move>& pv);

            SearchLimits limits_;
            SearchState state_;
        };

        SearchResult SearchWorker::search(const Board& board)
        {
            SearchResult result;
            Board working = board;
            const std::size_t max_depth = std::max<std::size_t>(1, limits_.depth);
            result.iterations.reserve(max_depth);

            for (std::size_t depth = 1; depth <= max_depth; ++depth)
            {
                state_.max_quiescence_ply = depth + limits_.quiescence_depth;
                SearchIteration iteration = search_root(working, depth, result.best_move);

                result.best_move = iteration.best_move;
                result.pv = iteration.pv;
                result.score = iteration.score;
                result.nodes = iteration.nodes;
                result.depth = iteration.depth;
                result.selective_depth = iteration.selective_depth;
                result.iterations.push_back(iteration);

                if (limits_.on_iteration)
                {
                    limits_.on_iteration(iteration);
                }
            }

            return result;
        }

        SearchIteration SearchWorker::search_root(Board& board, std::size_t depth, Move previous_best)
        {
            Move root_tt_move = 0;
            if (const auto* root_entry = state_.table.probe(board.key()))
            {
                root_tt_move = root_entry->best_move;
            }

            const Move root_order_move = previous_best != 0 ? previous_best : root_tt_move;
            Move best_move = 0;
            std::vector<Move> best_pv;
            Score best_score = -kInfiniteScore;
            Score alpha = -kInfiniteScore;

            const auto loop = visit_ordered_moves(
                board, move_ordering(root_order_move, state_, 0), [](Move) { return true; },
                [&](Move move, std::size_t)
                {
                    std::vector<Move> child_pv;
                    const Score score = -alpha_beta(board, depth - 1, -kInfiniteScore, -alpha, 1, child_pv);
                    if (score > best_score)
                    {
                        best_score = score;
                        best_move = move;
                        update_pv(best_pv, move, child_pv);
                        alpha = std::max(alpha, score);
                    }
                    return false;
                });

            const Score score = !loop.generated_any ? (board.checkers() != 0 ? -kMateScore : 0) : best_score;
            if (best_move != 0)
            {
                state_.table.store(board.key(), depth, score, Bound::Exact, best_move);
            }

            return SearchIteration{
                best_move, best_pv, score, state_.nodes, depth, state_.selective_depth,
            };
        }

        Score SearchWorker::alpha_beta(Board& board, std::size_t depth, Score alpha, Score beta, std::size_t ply,
                                       std::vector<Move>& pv)
        {
            if (depth == 0)
            {
                return quiescence(board, alpha, beta, ply, pv);
            }

            ++state_.nodes;
            state_.selective_depth = std::max(state_.selective_depth, ply);
            const Score original_alpha = alpha;
            const bool pv_node = beta - alpha > 1;
            Move tt_move = 0;

            if (const auto* entry = state_.table.probe(board.key()))
            {
                tt_move = entry->best_move;
                if (entry->depth >= depth)
                {
                    if (!pv_node && entry->bound == Bound::Exact)
                    {
                        return entry->score;
                    }
                    if (entry->bound == Bound::Lower)
                    {
                        alpha = std::max(alpha, entry->score);
                    }
                    else if (entry->bound == Bound::Upper)
                    {
                        beta = std::min(beta, entry->score);
                    }

                    if (alpha >= beta)
                    {
                        return entry->score;
                    }
                }
            }

            const std::size_t child_depth = state_.enable_check_extension && board.checkers() != 0 ? depth : depth - 1;
            Score best_score = -kInfiniteScore;
            Move best_move = 0;

            const auto loop = visit_ordered_moves(
                board, move_ordering(tt_move, state_, ply), [](Move) { return true; },
                [&](Move move, std::size_t move_index)
                {
                    std::vector<Move> child_pv;
                    Score score = 0;
                    if (move_index == 0)
                    {
                        score = -alpha_beta(board, child_depth, -beta, -alpha, ply + 1, child_pv);
                    }
                    else
                    {
                        score = -alpha_beta(board, child_depth, -alpha - 1, -alpha, ply + 1, child_pv);
                        if (score > alpha && score < beta)
                        {
                            score = -alpha_beta(board, child_depth, -beta, -alpha, ply + 1, child_pv);
                        }
                    }

                    best_score = std::max(best_score, score);
                    if (score > alpha)
                    {
                        best_move = move;
                        update_pv(pv, move, child_pv);
                        alpha = score;
                    }
                    if (alpha >= beta)
                    {
                        record_quiet_cutoff(state_, move, depth, ply);
                        return true;
                    }
                    return false;
                });

            if (!loop.generated_any)
            {
                return board.checkers() != 0 ? -kMateScore + static_cast<Score>(ply) : 0;
            }

            const Bound bound = best_score <= original_alpha ? Bound::Upper
                                : best_score >= beta         ? Bound::Lower
                                                             : Bound::Exact;
            state_.table.store(board.key(), depth, best_score, bound, best_move);
            return best_score;
        }

        Score SearchWorker::quiescence(Board& board, Score alpha, Score beta, std::size_t ply, std::vector<Move>& pv)
        {
            ++state_.nodes;
            state_.selective_depth = std::max(state_.selective_depth, ply);
            pv.clear();

            const bool in_check = board.checkers() != 0;
            if (!in_check || ply >= state_.max_quiescence_ply)
            {
                const Score stand_pat = state_.evaluator.evaluate(board);
                if (stand_pat >= beta)
                {
                    return beta;
                }
                alpha = std::max(alpha, stand_pat);

                if (ply >= state_.max_quiescence_ply)
                {
                    return alpha;
                }
            }

            const auto loop = visit_ordered_moves(
                board, MoveOrdering{},
                [&](Move move)
                {
                    if (!in_check && !is_noisy(move))
                    {
                        return false;
                    }
                    return in_check || !is_noisy(move) || is_good_noisy(board, move);
                },
                [&](Move move, std::size_t)
                {
                    std::vector<Move> child_pv;
                    const Score score = -quiescence(board, -beta, -alpha, ply + 1, child_pv);
                    if (score >= beta)
                    {
                        return true;
                    }
                    if (score > alpha)
                    {
                        update_pv(pv, move, child_pv);
                        alpha = score;
                    }
                    return false;
                });

            if (!loop.generated_any)
            {
                return in_check ? -kMateScore + static_cast<Score>(ply) : alpha;
            }

            return alpha;
        }

    } // namespace

    SearchResult search(const Board& board, SearchLimits limits, TranspositionTable& table, const Evaluator& evaluator)
    {
        return SearchWorker{std::move(limits), table, evaluator}.search(board);
    }

} // namespace aurora::chess
