#include "search.hpp"

#include "movepick.hpp"
#include "see.hpp"
#include "thread.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <thread>
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
            std::size_t worker_id{0};
        };

        constexpr Score kInitialAspirationWindow = 25;
        constexpr Score kMinimumMateBound = kMateScore - 512;

        [[nodiscard]] constexpr bool is_mate_score(Score score) noexcept
        {
            return score <= -kMinimumMateBound || score >= kMinimumMateBound;
        }

        [[nodiscard]] bool has_non_pawn_material(const Board& board, Color color) noexcept
        {
            const Bitboard pieces = board.piece_bb(PieceType::Knight) | board.piece_bb(PieceType::Bishop) |
                                    board.piece_bb(PieceType::Rook) | board.piece_bb(PieceType::Queen);
            return (pieces & board.occupancy(color)) != 0;
        }

        [[nodiscard]] constexpr std::size_t futility_move_count(std::size_t depth) noexcept
        {
            return (3 + depth * depth) / 2;
        }

        [[nodiscard]] std::size_t late_move_reduction(std::size_t depth, std::size_t move_number, bool pv_node,
                                                      bool noisy, bool gives_check, int history_score) noexcept
        {
            if (depth < 2 || move_number <= 1)
            {
                return 0;
            }

            const double base =
                0.45 + std::log(static_cast<double>(depth)) * std::log(static_cast<double>(move_number)) / 2.15;
            auto reduction = static_cast<std::size_t>(std::max(0.0, base));

            if (!pv_node && move_number >= 6)
            {
                ++reduction;
            }
            if (!pv_node && depth >= 7 && move_number >= 10)
            {
                ++reduction;
            }
            if (pv_node && reduction > 0)
            {
                --reduction;
            }
            if ((noisy || gives_check) && reduction > 0)
            {
                --reduction;
            }
            if (history_score > 4000 && reduction > 0)
            {
                --reduction;
            }
            else if (history_score < 256 && !pv_node)
            {
                ++reduction;
            }

            return std::min(reduction, depth - 1);
        }

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
                state_.worker_id = limits_.worker_id;
            }

            [[nodiscard]] SearchResult search(const Board& board);

        private:
            [[nodiscard]] SearchIteration search_root(Board& board, std::size_t depth, Move previous_best, Score alpha,
                                                      Score beta);
            [[nodiscard]] Score alpha_beta(Board& board, std::size_t depth, Score alpha, Score beta, std::size_t ply,
                                           std::vector<Move>& pv, bool allow_null = true);
            [[nodiscard]] Score quiescence(Board& board, Score alpha, Score beta, std::size_t ply,
                                           std::vector<Move>& pv);
            [[nodiscard]] bool stopped() const noexcept;

            SearchLimits limits_;
            SearchState state_;
        };

        bool SearchWorker::stopped() const noexcept
        {
            return limits_.stop != nullptr && limits_.stop->load(std::memory_order_relaxed);
        }

        SearchResult SearchWorker::search(const Board& board)
        {
            SearchResult result;
            Board working = board;
            const std::size_t max_depth = std::max<std::size_t>(1, limits_.depth);
            result.iterations.reserve(max_depth);

            for (std::size_t depth = 1; depth <= max_depth; ++depth)
            {
                if (stopped())
                {
                    break;
                }

                state_.max_quiescence_ply = depth + limits_.quiescence_depth;
                SearchIteration iteration;
                Score alpha = -kInfiniteScore;
                Score beta = kInfiniteScore;
                Score delta = kInitialAspirationWindow + static_cast<Score>(state_.worker_id % 8);

                if (depth >= 4 && result.best_move != 0 && !is_mate_score(result.score))
                {
                    alpha = std::max(result.score - delta, -kInfiniteScore);
                    beta = std::min(result.score + delta, kInfiniteScore);
                }

                while (true)
                {
                    iteration = search_root(working, depth, result.best_move, alpha, beta);
                    const bool full_window = alpha == -kInfiniteScore && beta == kInfiniteScore;
                    if (stopped() || full_window || (iteration.score > alpha && iteration.score < beta))
                    {
                        break;
                    }

                    if (iteration.score <= alpha)
                    {
                        beta = alpha;
                        alpha = std::max(iteration.score - delta, -kInfiniteScore);
                    }
                    else
                    {
                        alpha = std::max(beta - delta, -kInfiniteScore);
                        beta = std::min(iteration.score + delta, kInfiniteScore);
                    }

                    delta += delta / 2 + 8;
                }

                if (stopped() && iteration.best_move == 0)
                {
                    break;
                }

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

        SearchIteration SearchWorker::search_root(Board& board, std::size_t depth, Move previous_best, Score alpha,
                                                  Score beta)
        {
            Move root_tt_move = 0;
            if (const auto root_entry = state_.table.probe(board.key()))
            {
                root_tt_move = root_entry->best_move;
            }

            const Move root_order_move = previous_best != 0 ? previous_best : root_tt_move;
            std::vector<Move> root_moves;
            MovePicker picker{board, move_ordering(root_order_move, state_, 0)};
            for (Move move = picker.next(); move != 0; move = picker.next())
            {
                if (board.make_move(move))
                {
                    root_moves.push_back(move);
                    board.undo_move();
                }
            }

            if (root_moves.empty())
            {
                return SearchIteration{
                    0, {}, board.checkers() != 0 ? -kMateScore : 0, state_.nodes, depth, state_.selective_depth,
                };
            }

            Move best_move = 0;
            std::vector<Move> best_pv;
            Score best_score = -kInfiniteScore;
            const Score original_alpha = alpha;
            const std::size_t first_move = state_.worker_id == 0 ? 0 : state_.worker_id % root_moves.size();

            for (std::size_t searched = 0; searched < root_moves.size(); ++searched)
            {
                if (stopped())
                {
                    break;
                }

                const Move move = root_moves[(first_move + searched) % root_moves.size()];
                if (!board.make_move(move))
                {
                    continue;
                }

                std::vector<Move> child_pv;
                const Score score = -alpha_beta(board, depth - 1, -beta, -alpha, 1, child_pv);
                board.undo_move();

                if (score > best_score)
                {
                    best_score = score;
                    best_move = move;
                    update_pv(best_pv, move, child_pv);
                    alpha = std::max(alpha, score);
                }
                if (alpha >= beta)
                {
                    break;
                }
            }

            const Score score = best_move == 0 ? (board.checkers() != 0 ? -kMateScore : 0) : best_score;
            if (best_move != 0)
            {
                const Bound bound = score <= original_alpha ? Bound::Upper
                                    : score >= beta         ? Bound::Lower
                                                            : Bound::Exact;
                state_.table.store(board.key(), depth, score, bound, best_move);
            }

            return SearchIteration{
                best_move, best_pv, score, state_.nodes, depth, state_.selective_depth,
            };
        }

        [[nodiscard]] bool is_better_result(const SearchResult& candidate, const SearchResult& best) noexcept
        {
            if (candidate.best_move == 0)
            {
                return false;
            }
            if (best.best_move == 0)
            {
                return true;
            }
            if (candidate.depth != best.depth)
            {
                return candidate.depth > best.depth;
            }
            if (candidate.pv.size() != best.pv.size())
            {
                return candidate.pv.size() > best.pv.size();
            }

            return candidate.score > best.score;
        }

        Score SearchWorker::alpha_beta(Board& board, std::size_t depth, Score alpha, Score beta, std::size_t ply,
                                       std::vector<Move>& pv, bool allow_null)
        {
            if (stopped())
            {
                pv.clear();
                return state_.evaluator.evaluate(board);
            }

            if (depth == 0)
            {
                return quiescence(board, alpha, beta, ply, pv);
            }

            alpha = std::max(alpha, -kMateScore + static_cast<Score>(ply));
            beta = std::min(beta, kMateScore - static_cast<Score>(ply + 1));
            if (alpha >= beta)
            {
                return alpha;
            }

            ++state_.nodes;
            state_.selective_depth = std::max(state_.selective_depth, ply);
            const Score original_alpha = alpha;
            const bool pv_node = beta - alpha > 1;
            Move tt_move = 0;
            const bool in_check = board.checkers() != 0;
            Score static_eval = 0;
            bool static_eval_ready = false;
            auto evaluate_static = [&]() noexcept
            {
                if (!static_eval_ready)
                {
                    static_eval = state_.evaluator.evaluate(board);
                    static_eval_ready = true;
                }
                return static_eval;
            };

            if (const auto entry = state_.table.probe(board.key()))
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

            if (!pv_node && !in_check && depth <= 4 && !is_mate_score(beta))
            {
                const Score margin = static_cast<Score>(95 * depth);
                const Score eval = evaluate_static();
                if (eval - margin >= beta)
                {
                    return beta + (eval - beta) / 3;
                }
            }

            if (allow_null && !pv_node && !in_check && depth >= 3 && !is_mate_score(beta) &&
                has_non_pawn_material(board, board.side_to_move()) && evaluate_static() >= beta)
            {
                const Score eval_margin = std::max<Score>(0, evaluate_static() - beta);
                const std::size_t reduction =
                    std::min<std::size_t>(depth, 3 + depth / 3 + static_cast<std::size_t>(eval_margin / 200));
                const std::size_t null_depth = depth > reduction ? depth - reduction : 0;
                if (board.make_null_move())
                {
                    std::vector<Move> null_pv;
                    const Score score = -alpha_beta(board, null_depth, -beta, -beta + 1, ply + 1, null_pv, false);
                    board.undo_move();
                    if (score >= beta)
                    {
                        return score;
                    }
                }
            }

            if (!pv_node && depth >= 5 && tt_move == 0)
            {
                --depth;
            }

            if (!pv_node && !in_check && depth >= 4 && !is_mate_score(beta))
            {
                const Score prob_cut_beta = beta + 175;
                MovePicker prob_picker{board, move_ordering(tt_move, state_, ply)};
                for (Move move = prob_picker.next(); move != 0; move = prob_picker.next())
                {
                    if (!is_noisy(move) || !see_ge(board, move, prob_cut_beta - evaluate_static()))
                    {
                        continue;
                    }

                    if (!board.make_move(move))
                    {
                        continue;
                    }

                    std::vector<Move> prob_pv;
                    Score score = -quiescence(board, -prob_cut_beta, -prob_cut_beta + 1, ply + 1, prob_pv);
                    if (score >= prob_cut_beta)
                    {
                        const std::size_t prob_depth = depth > 4 ? depth - 4 : 0;
                        score =
                            -alpha_beta(board, prob_depth, -prob_cut_beta, -prob_cut_beta + 1, ply + 1, prob_pv, false);
                    }
                    board.undo_move();

                    if (score >= prob_cut_beta)
                    {
                        state_.table.store(board.key(), depth, score, Bound::Lower, move);
                        return score - (prob_cut_beta - beta);
                    }
                }
            }

            const std::size_t child_depth = state_.enable_check_extension && in_check ? depth : depth - 1;
            Score best_score = -kInfiniteScore;
            Move best_move = 0;

            MovePicker picker{board, move_ordering(tt_move, state_, ply)};
            const bool generated_any = picker.generated_any();
            std::size_t searched = 0;
            for (Move move = picker.next(); move != 0; move = picker.next())
            {
                if (stopped())
                {
                    break;
                }

                const std::size_t move_number = searched + 1;
                const bool noisy = is_noisy(move);
                const bool quiet_late_move = !noisy && move != tt_move && move_number >= 4;
                const int history_score = state_.history[static_cast<std::size_t>(move)];
                const std::size_t estimated_reduction =
                    late_move_reduction(child_depth, move_number, pv_node, noisy, false, history_score);
                const std::size_t estimated_lmr_depth =
                    child_depth > estimated_reduction ? child_depth - estimated_reduction : 0;

                const bool noisy_see_bad = !pv_node && !in_check && best_move != 0 && noisy && depth <= 8 &&
                                           !see_ge(board, move, -static_cast<Score>(150 * depth));
                const bool quiet_see_bad =
                    !pv_node && !in_check && best_move != 0 && quiet_late_move && depth <= 8 &&
                    !see_ge(board, move, -static_cast<Score>(30 * estimated_lmr_depth * estimated_lmr_depth));

                if (!board.make_move(move))
                {
                    continue;
                }

                ++searched;
                const bool gives_check = board.checkers() != 0;

                if (!gives_check && (noisy_see_bad || quiet_see_bad))
                {
                    board.undo_move();
                    continue;
                }

                if (!pv_node && !in_check && !gives_check && best_move != 0 && quiet_late_move && depth <= 8)
                {
                    if (move_number >= futility_move_count(depth))
                    {
                        board.undo_move();
                        continue;
                    }

                    if (!is_mate_score(alpha))
                    {
                        const Score margin = static_cast<Score>(120 * estimated_lmr_depth + 80);
                        const Score futility_score = evaluate_static() + margin;
                        if (futility_score <= alpha)
                        {
                            best_score = std::max(best_score, futility_score);
                            board.undo_move();
                            continue;
                        }
                    }
                }

                std::vector<Move> child_pv;
                Score score = 0;
                const std::size_t reduction =
                    !in_check && !gives_check
                        ? late_move_reduction(child_depth, move_number, pv_node, noisy, gives_check, history_score)
                        : 0;
                if (move_number == 1)
                {
                    score = -alpha_beta(board, child_depth, -beta, -alpha, ply + 1, child_pv);
                }
                else
                {
                    std::size_t search_depth = child_depth;
                    if (reduction != 0)
                    {
                        search_depth = child_depth - reduction;
                    }

                    score = -alpha_beta(board, search_depth, -alpha - 1, -alpha, ply + 1, child_pv);
                    if (reduction != 0 && score > alpha)
                    {
                        score = -alpha_beta(board, child_depth, -alpha - 1, -alpha, ply + 1, child_pv);
                    }
                    if (score > alpha && score < beta)
                    {
                        score = -alpha_beta(board, child_depth, -beta, -alpha, ply + 1, child_pv);
                    }
                }

                board.undo_move();

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
                    break;
                }
            }

            if (!generated_any)
            {
                return in_check ? -kMateScore + static_cast<Score>(ply) : 0;
            }

            if (best_score == -kInfiniteScore)
            {
                best_score = alpha;
            }

            const Bound bound = best_score <= original_alpha ? Bound::Upper
                                : best_score >= beta         ? Bound::Lower
                                                             : Bound::Exact;
            if (!stopped())
            {
                state_.table.store(board.key(), depth, best_score, bound, best_move);
            }
            return best_score;
        }

        Score SearchWorker::quiescence(Board& board, Score alpha, Score beta, std::size_t ply, std::vector<Move>& pv)
        {
            if (stopped())
            {
                pv.clear();
                return state_.evaluator.evaluate(board);
            }

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
                    if (stopped())
                    {
                        return true;
                    }

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

        [[nodiscard]] SearchResult select_lazy_smp_result(const std::vector<SearchResult>& worker_results)
        {
            SearchResult combined = worker_results.empty() ? SearchResult{} : worker_results.front();
            std::uint64_t nodes = 0;
            std::size_t selective_depth = 0;
            SearchResult best_helper;

            for (const auto& result : worker_results)
            {
                nodes += result.nodes;
                selective_depth = std::max(selective_depth, result.selective_depth);
                if (is_better_result(result, best_helper))
                {
                    best_helper = result;
                }
            }

            if (combined.best_move == 0 && best_helper.best_move != 0)
            {
                combined = best_helper;
            }

            combined.nodes = nodes;
            combined.selective_depth = std::max(combined.selective_depth, selective_depth);
            return combined;
        }

        [[nodiscard]] SearchResult search_lazy_smp(const Board& board, SearchLimits limits, TranspositionTable& table,
                                                   const Evaluator& evaluator, std::size_t worker_count)
        {
            std::vector<SearchResult> worker_results(worker_count);
            auto run_worker = [&](std::size_t worker_id)
            {
                if (worker_id >= worker_count)
                {
                    return;
                }

                SearchLimits worker_limits = limits;
                worker_limits.threads = 1;
                worker_limits.worker_id = worker_id;
                worker_limits.thread_pool = nullptr;
                if (worker_id != 0)
                {
                    worker_limits.on_iteration = {};
                }

                worker_results[worker_id] = SearchWorker{std::move(worker_limits), table, evaluator}.search(board);
            };

            if (limits.thread_pool != nullptr && limits.thread_pool->size() > 1)
            {
                limits.thread_pool->run_with_workers(run_worker);
            }
            else
            {
                std::vector<std::thread> workers;
                workers.reserve(worker_count - 1);
                for (std::size_t worker_id = 1; worker_id < worker_count; ++worker_id)
                {
                    workers.emplace_back([&, worker_id] { run_worker(worker_id); });
                }

                run_worker(0);

                for (auto& worker : workers)
                {
                    worker.join();
                }
            }

            return select_lazy_smp_result(worker_results);
        }

    } // namespace

    SearchResult search(const Board& board, SearchLimits limits, TranspositionTable& table, const Evaluator& evaluator)
    {
        const std::size_t worker_count = std::max<std::size_t>(
            1, std::min<std::size_t>(limits.threads,
                                     limits.thread_pool != nullptr ? limits.thread_pool->size() : limits.threads));
        if (worker_count > 1)
        {
            return search_lazy_smp(board, std::move(limits), table, evaluator, worker_count);
        }

        return SearchWorker{std::move(limits), table, evaluator}.search(board);
    }

} // namespace aurora::chess
