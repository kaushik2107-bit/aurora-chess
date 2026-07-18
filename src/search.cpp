#include "search.hpp"

#include "movepick.hpp"
#include "nnue.hpp"
#include "see.hpp"
#include "thread.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
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
            std::vector<Move> counter_moves{std::vector<Move>(1u << 16)};
            std::vector<int> continuation_history{
                std::vector<int>(kPieceSquareHistoryBuckets * kPieceSquareHistoryBuckets)};
            bool enable_check_extension{true};
            std::size_t worker_id{0};
        };

        struct SearchStack
        {
            Score static_eval{0};
            Move tt_move{0};
            Move previous_move{0};
            std::size_t previous_piece_square{kNoPieceSquareHistory};
            bool static_eval_ready{false};
            bool in_check{false};
        };

        constexpr Score kInitialAspirationWindow = 25;
        constexpr Score kMinimumMateBound = kMateScore - 512;
        constexpr int kMaxHistoryScore = 1'000'000;
        constexpr std::size_t kRootPvsDepth = 7;
        constexpr std::size_t kShallowRootVerificationMoves = 16;
        constexpr std::size_t kAccumulatorStackSize = 256;
        constexpr std::size_t kSearchStackSize = 256;
        constexpr std::array<Score, static_cast<std::size_t>(PieceType::Count)> kPieceValues{
            100, 320, 330, 500, 900, 0,
        };

        [[nodiscard]] constexpr bool is_mate_score(Score score) noexcept
        {
            return score <= -kMinimumMateBound || score >= kMinimumMateBound;
        }

        [[nodiscard]] constexpr Score score_to_tt(Score score, std::size_t ply) noexcept
        {
            if (score >= kMinimumMateBound)
            {
                return score + static_cast<Score>(ply);
            }
            if (score <= -kMinimumMateBound)
            {
                return score - static_cast<Score>(ply);
            }
            return score;
        }

        [[nodiscard]] constexpr Score score_from_tt(Score score, std::size_t ply) noexcept
        {
            if (score >= kMinimumMateBound)
            {
                return score - static_cast<Score>(ply);
            }
            if (score <= -kMinimumMateBound)
            {
                return score + static_cast<Score>(ply);
            }
            return score;
        }

        [[nodiscard]] bool has_non_pawn_material(const Board& board, Color color) noexcept
        {
            const Bitboard pieces = board.piece_bb(PieceType::Knight) | board.piece_bb(PieceType::Bishop) |
                                    board.piece_bb(PieceType::Rook) | board.piece_bb(PieceType::Queen);
            return (pieces & board.occupancy(color)) != 0;
        }

        [[nodiscard]] constexpr std::size_t futility_move_count(std::size_t depth, bool improving) noexcept
        {
            return (3 + depth * depth) / (improving ? 1 : 2);
        }

        [[nodiscard]] constexpr Score reverse_futility_margin(std::size_t depth, Move tt_move, bool improving) noexcept
        {
            const Score tt_bonus = tt_move != 0 ? 20 : 0;
            const Score improving_bonus = improving ? static_cast<Score>(45 * depth) : 0;
            return static_cast<Score>(90 * depth) - improving_bonus - tt_bonus;
        }

        [[nodiscard]] constexpr Score razor_margin(std::size_t depth) noexcept
        {
            return static_cast<Score>(250 + 100 * depth * depth);
        }

        [[nodiscard]] constexpr std::size_t null_move_reduction(std::size_t depth, Score eval_margin) noexcept
        {
            const auto margin_bonus =
                static_cast<std::size_t>(std::min<Score>(3, std::max<Score>(0, eval_margin / 160)));
            return std::min<std::size_t>(depth, 4 + depth / 3 + margin_bonus);
        }

        [[nodiscard]] constexpr Score quiet_futility_margin(std::size_t reduced_depth, int history_score,
                                                            Move tt_move) noexcept
        {
            const Score history_bonus = history_score > 4000 ? 80 : history_score < -4000 ? -60 : 0;
            const Score tt_bonus = tt_move != 0 ? 20 : 0;
            return static_cast<Score>(95 * reduced_depth + 70 + history_bonus - tt_bonus);
        }

        [[nodiscard]] constexpr Score piece_value(Piece piece) noexcept
        {
            return piece == Piece::None ? 0 : kPieceValues[static_cast<std::size_t>(piece_type(piece))];
        }

        [[nodiscard]] Score captured_piece_value(const Board& board, Move move) noexcept
        {
            if (move_flag(move) == MoveFlag::EnPassant)
            {
                return kPieceValues[static_cast<std::size_t>(PieceType::Pawn)];
            }

            return piece_value(board.piece_on(move_to(move)));
        }

        [[nodiscard]] int continuation_history_score(const SearchState& state, std::size_t previous_piece_square,
                                                     std::size_t current_piece_square) noexcept
        {
            if (previous_piece_square == kNoPieceSquareHistory || current_piece_square == kNoPieceSquareHistory)
            {
                return 0;
            }

            return state
                .continuation_history[previous_piece_square * kPieceSquareHistoryBuckets + current_piece_square];
        }

        [[nodiscard]] int quiet_history_score(const SearchState& state, Move move, std::size_t previous_piece_square,
                                              std::size_t current_piece_square) noexcept
        {
            return state.history[static_cast<std::size_t>(move)] +
                   continuation_history_score(state, previous_piece_square, current_piece_square);
        }

        void apply_history_update(int& score, int bonus) noexcept
        {
            score = std::clamp(score + bonus, -kMaxHistoryScore, kMaxHistoryScore);
        }

        void update_quiet_history(SearchState& state, Move move, int bonus, Move previous_move,
                                  std::size_t previous_piece_square, std::size_t current_piece_square)
        {
            apply_history_update(state.history[static_cast<std::size_t>(move)], bonus);

            if (bonus > 0 && previous_move != 0)
            {
                state.counter_moves[static_cast<std::size_t>(previous_move)] = move;
            }

            if (previous_piece_square != kNoPieceSquareHistory && current_piece_square != kNoPieceSquareHistory)
            {
                int& continuation_score =
                    state.continuation_history[previous_piece_square * kPieceSquareHistoryBuckets +
                                               current_piece_square];
                apply_history_update(continuation_score, bonus);
            }
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

        [[nodiscard]] MoveOrdering move_ordering(Move tt_move, const SearchState& state, std::size_t ply,
                                                 Move previous_move = 0,
                                                 std::size_t previous_piece_square = kNoPieceSquareHistory)
        {
            MoveOrdering ordering;
            ordering.tt_move = tt_move;
            ordering.history = &state.history;
            ordering.continuation_history = &state.continuation_history;
            ordering.previous_piece_square = previous_piece_square;
            if (previous_move != 0)
            {
                ordering.counter_move = state.counter_moves[static_cast<std::size_t>(previous_move)];
            }

            if (ply < state.killers[0].size())
            {
                ordering.killers = {state.killers[0][ply], state.killers[1][ply]};
            }

            return ordering;
        }

        void record_quiet_cutoff(SearchState& state, Move move, std::size_t depth, std::size_t ply, Move previous_move,
                                 std::size_t previous_piece_square, std::size_t current_piece_square)
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

            const int bonus = static_cast<int>(depth * depth);
            update_quiet_history(state, move, bonus, previous_move, previous_piece_square, current_piece_square);
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
            [[nodiscard]] Score evaluate(const Board& board) const noexcept;
            bool make_search_move(Board& board, Move move, Board::UndoState& undo_state);
            void undo_search_move(Board& board);
            void reset_accumulator(const Board& board);
            void push_accumulator(const Board& board);
            void pop_accumulator() noexcept;
            void count_node() noexcept;

            [[nodiscard]] SearchIteration search_root(Board& board, std::size_t depth, Move previous_best, Score alpha,
                                                      Score beta);
            [[nodiscard]] Score alpha_beta(Board& board, std::size_t depth, Score alpha, Score beta, std::size_t ply,
                                           std::vector<Move>& pv, bool allow_null = true, bool conservative = false);
            [[nodiscard]] Score quiescence(Board& board, Score alpha, Score beta, std::size_t ply,
                                           std::vector<Move>& pv);
            [[nodiscard]] bool stopped() const noexcept;

            SearchLimits limits_;
            SearchState state_;
            const NnueEvaluator* nnue_{nullptr};
            std::array<NnueEvaluator::Accumulator, kAccumulatorStackSize> accumulator_stack_{};
            std::array<SearchStack, kSearchStackSize> search_stack_{};
            std::size_t accumulator_stack_size_{0};
        };

        bool SearchWorker::stopped() const noexcept
        {
            if (limits_.stop != nullptr && limits_.stop->load(std::memory_order_relaxed))
            {
                return true;
            }

            if (limits_.shared_deadline_ms != nullptr)
            {
                const std::int64_t deadline_ms = limits_.shared_deadline_ms->load(std::memory_order_relaxed);
                if (deadline_ms > 0)
                {
                    const auto now = std::chrono::steady_clock::now().time_since_epoch();
                    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                    if (now_ms >= deadline_ms)
                    {
                        return true;
                    }
                }
            }

            return limits_.deadline && std::chrono::steady_clock::now() >= *limits_.deadline;
        }

        Score SearchWorker::evaluate(const Board& board) const noexcept
        {
            if (nnue_ != nullptr && accumulator_stack_size_ != 0)
            {
                return nnue_->evaluate(board, accumulator_stack_[accumulator_stack_size_ - 1]);
            }
            return state_.evaluator.evaluate(board);
        }

        void SearchWorker::reset_accumulator(const Board& board)
        {
            nnue_ = dynamic_cast<const NnueEvaluator*>(&state_.evaluator);
            accumulator_stack_size_ = 0;
            if (nnue_ == nullptr || !nnue_->is_loaded())
            {
                nnue_ = nullptr;
                return;
            }

            accumulator_stack_size_ = 1;
            nnue_->refresh_accumulator(board, accumulator_stack_[0]);
        }

        void SearchWorker::push_accumulator(const Board& board)
        {
            if (nnue_ == nullptr)
            {
                return;
            }
            if (accumulator_stack_size_ == 0 || accumulator_stack_size_ >= accumulator_stack_.size())
            {
                return;
            }

            nnue_->update_accumulator(accumulator_stack_[accumulator_stack_size_ - 1], board, board.last_dirty_piece(),
                                      accumulator_stack_[accumulator_stack_size_]);
            ++accumulator_stack_size_;
        }

        void SearchWorker::pop_accumulator() noexcept
        {
            if (nnue_ != nullptr && accumulator_stack_size_ > 1)
            {
                --accumulator_stack_size_;
            }
        }

        bool SearchWorker::make_search_move(Board& board, Move move, Board::UndoState& undo_state)
        {
            if (!board.make_move(move, undo_state))
            {
                return false;
            }
            push_accumulator(board);
            return true;
        }

        void SearchWorker::undo_search_move(Board& board)
        {
            pop_accumulator();
            board.undo_move();
        }

        void SearchWorker::count_node() noexcept
        {
            ++state_.nodes;
            if (limits_.shared_nodes != nullptr)
            {
                limits_.shared_nodes->fetch_add(1, std::memory_order_relaxed);
            }
        }

        SearchResult SearchWorker::search(const Board& board)
        {
            SearchResult result;
            SearchIteration fallback;
            Board working = board;
            reset_accumulator(working);
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

                if (!iteration.completed)
                {
                    if (result.best_move == 0 && iteration.best_move != 0)
                    {
                        fallback = iteration;
                    }
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

                if (stopped())
                {
                    break;
                }
            }

            if (result.best_move == 0 && fallback.best_move != 0)
            {
                result.best_move = fallback.best_move;
                result.pv = fallback.pv;
                result.score = fallback.score;
                result.nodes = fallback.nodes;
                result.selective_depth = fallback.selective_depth;
            }

            return result;
        }

        SearchIteration SearchWorker::search_root(Board& board, std::size_t depth, Move previous_best, Score alpha,
                                                  Score beta)
        {
            if (board.is_draw())
            {
                return SearchIteration{0, {}, 0, state_.nodes, depth, state_.selective_depth};
            }

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
            bool completed = true;

            for (std::size_t searched = 0; searched < root_moves.size(); ++searched)
            {
                if (stopped())
                {
                    completed = false;
                    break;
                }

                const Move move = root_moves[(first_move + searched) % root_moves.size()];
                Board::UndoState undo_state;
                const std::size_t current_piece_square =
                    piece_square_history_index(board.piece_on(move_from(move)), move_to(move));
                if (!make_search_move(board, move, undo_state))
                {
                    continue;
                }

                std::vector<Move> child_pv;
                search_stack_[1].previous_move = move;
                search_stack_[1].previous_piece_square = current_piece_square;
                const bool use_root_pvs =
                    searched != 0 && (depth >= kRootPvsDepth || searched <= kShallowRootVerificationMoves);
                Score score = 0;
                if (!use_root_pvs)
                {
                    score = -alpha_beta(board, depth - 1, -beta, -alpha, 1, child_pv);
                }
                else
                {
                    score = -alpha_beta(board, depth - 1, -alpha - 1, -alpha, 1, child_pv, false, true);
                    if (score > alpha)
                    {
                        score = -alpha_beta(board, depth - 1, -beta, -alpha, 1, child_pv);
                    }
                }
                undo_search_move(board);

                if (stopped())
                {
                    completed = false;
                    break;
                }

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
            if (best_move != 0 && completed)
            {
                const Bound bound = score <= original_alpha ? Bound::Upper
                                    : score >= beta         ? Bound::Lower
                                                            : Bound::Exact;
                state_.table.store(board.key(), depth, score_to_tt(score, 0), bound, best_move);
            }

            return SearchIteration{
                best_move, best_pv, score, state_.nodes, depth, state_.selective_depth, completed,
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
                                       std::vector<Move>& pv, bool allow_null, bool conservative)
        {
            if (stopped())
            {
                pv.clear();
                return evaluate(board);
            }

            if (board.is_draw())
            {
                pv.clear();
                return 0;
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

            count_node();
            state_.selective_depth = std::max(state_.selective_depth, ply);
            const Score original_alpha = alpha;
            const bool pv_node = beta - alpha > 1;
            const bool pruning_node = !conservative;
            const bool child_conservative = conservative && ply < 3;
            auto& stack = search_stack_[std::min(ply, search_stack_.size() - 1)];
            const Move previous_move = stack.previous_move;
            const std::size_t previous_piece_square = stack.previous_piece_square;
            stack = {};
            stack.previous_move = previous_move;
            stack.previous_piece_square = previous_piece_square;
            stack.in_check = board.checkers() != 0;
            const bool in_check = stack.in_check;
            std::optional<TranspositionEntry> tt_entry = state_.table.probe(board.key());
            if (tt_entry)
            {
                stack.tt_move = tt_entry->best_move;
                if (tt_entry->has_static_eval)
                {
                    stack.static_eval = tt_entry->static_eval;
                    stack.static_eval_ready = true;
                }
            }
            const Move tt_move = stack.tt_move;
            auto evaluate_static = [&]() noexcept -> Score
            {
                if (!stack.static_eval_ready)
                {
                    stack.static_eval = evaluate(board);
                    stack.static_eval_ready = true;
                }
                return stack.static_eval;
            };
            auto is_improving = [&]() noexcept -> bool
            {
                if (ply < 2)
                {
                    return false;
                }

                const auto& previous_same_side = search_stack_[std::min(ply - 2, search_stack_.size() - 1)];
                return stack.static_eval_ready && previous_same_side.static_eval_ready &&
                       !previous_same_side.in_check && stack.static_eval > previous_same_side.static_eval;
            };

            if (!pv_node && tt_entry)
            {
                const Score tt_score = score_from_tt(tt_entry->score, ply);
                if (tt_entry->depth >= depth)
                {
                    if (!pv_node && tt_entry->bound == Bound::Exact)
                    {
                        return tt_score;
                    }
                    if (tt_entry->bound == Bound::Lower)
                    {
                        alpha = std::max(alpha, tt_score);
                    }
                    else if (tt_entry->bound == Bound::Upper)
                    {
                        beta = std::min(beta, tt_score);
                    }

                    if (alpha >= beta)
                    {
                        return tt_score;
                    }
                }
            }

            // At shallow, clearly failing nodes, quiescence can establish an upper bound much more cheaply than
            // searching every quiet move. The quadratic margin keeps the pruning deliberately conservative as the
            // remaining depth grows.
            if (pruning_node && !pv_node && !in_check && depth <= 3 && !is_mate_score(alpha) &&
                evaluate_static() + razor_margin(depth) <= alpha)
            {
                std::vector<Move> razor_pv;
                const Score razor_score = quiescence(board, alpha - 1, alpha, ply, razor_pv);
                if (razor_score <= alpha)
                {
                    pv.clear();
                    return razor_score;
                }
            }

            if (pruning_node && !pv_node && !in_check && depth <= 8 && !is_mate_score(beta))
            {
                const Score eval = evaluate_static();
                const Score margin = reverse_futility_margin(depth, tt_move, is_improving());
                if (eval - margin >= beta)
                {
                    return beta + (eval - beta) / 3;
                }
            }

            if (pruning_node && allow_null && !pv_node && !in_check && depth >= 3 && !is_mate_score(beta) &&
                has_non_pawn_material(board, board.side_to_move()) && evaluate_static() >= beta)
            {
                const Score eval_margin = std::max<Score>(0, evaluate_static() - beta);
                const std::size_t reduction = null_move_reduction(depth, eval_margin);
                const std::size_t null_depth = depth > reduction ? depth - reduction : 0;
                Board::UndoState null_state;
                if (board.make_null_move(null_state))
                {
                    std::vector<Move> null_pv;
                    if (ply + 1 < search_stack_.size())
                    {
                        search_stack_[ply + 1].previous_move = 0;
                        search_stack_[ply + 1].previous_piece_square = kNoPieceSquareHistory;
                    }
                    const Score score =
                        -alpha_beta(board, null_depth, -beta, -beta + 1, ply + 1, null_pv, false, child_conservative);
                    board.undo_move();
                    if (score >= beta)
                    {
                        return score;
                    }
                }
            }

            if (pruning_node && !pv_node && depth >= 5 && tt_move == 0)
            {
                --depth;
            }

            if (pruning_node && !pv_node && !in_check && depth >= 4 && !is_mate_score(beta))
            {
                const Score prob_cut_beta = beta + (is_improving() ? 145 : 190);
                const bool tt_refutes_prob_cut =
                    tt_entry && (tt_entry->bound == Bound::Exact || tt_entry->bound == Bound::Upper) &&
                    score_from_tt(tt_entry->score, ply) < prob_cut_beta;
                if (!tt_refutes_prob_cut)
                {
                    MovePicker prob_picker{board,
                                           move_ordering(tt_move, state_, ply, previous_move, previous_piece_square)};
                    for (Move move = prob_picker.next(); move != 0; move = prob_picker.next())
                    {
                        if (!is_noisy(move) || !see_ge(board, move, prob_cut_beta - evaluate_static()))
                        {
                            continue;
                        }

                        const std::size_t current_piece_square =
                            piece_square_history_index(board.piece_on(move_from(move)), move_to(move));
                        Board::UndoState undo_state;
                        if (!make_search_move(board, move, undo_state))
                        {
                            continue;
                        }

                        std::vector<Move> prob_pv;
                        if (ply + 1 < search_stack_.size())
                        {
                            search_stack_[ply + 1].previous_move = move;
                            search_stack_[ply + 1].previous_piece_square = current_piece_square;
                        }
                        Score score = -quiescence(board, -prob_cut_beta, -prob_cut_beta + 1, ply + 1, prob_pv);
                        if (score >= prob_cut_beta)
                        {
                            const std::size_t prob_depth = depth > 4 ? depth - 4 : 0;
                            score = -alpha_beta(board, prob_depth, -prob_cut_beta, -prob_cut_beta + 1, ply + 1,
                                                prob_pv, false);
                        }
                        undo_search_move(board);

                        if (score >= prob_cut_beta)
                        {
                            state_.table.store(board.key(), depth, score_to_tt(score, ply), Bound::Lower, move,
                                               stack.static_eval_ready ? std::optional<Score>{stack.static_eval}
                                                                       : std::nullopt);
                            return score - (prob_cut_beta - beta);
                        }
                    }
                }
            }

            const std::size_t child_depth = state_.enable_check_extension && in_check ? depth : depth - 1;
            Score best_score = -kInfiniteScore;
            Move best_move = 0;

            MovePicker picker{board, move_ordering(tt_move, state_, ply, previous_move, previous_piece_square)};
            const bool generated_any = picker.generated_any();
            std::size_t searched = 0;
            std::array<Move, MoveList::kMaxMoves> searched_quiets{};
            std::array<std::size_t, MoveList::kMaxMoves> searched_quiet_piece_squares{};
            std::size_t searched_quiet_count = 0;
            for (Move move = picker.next(); move != 0; move = picker.next())
            {
                if (stopped())
                {
                    break;
                }

                const std::size_t move_number = searched + 1;
                const bool noisy = is_noisy(move);
                const bool quiet_late_move = !noisy && move != tt_move && move_number >= 4;
                const std::size_t current_piece_square =
                    piece_square_history_index(board.piece_on(move_from(move)), move_to(move));
                const int history_score =
                    noisy ? 0 : quiet_history_score(state_, move, previous_piece_square, current_piece_square);
                const std::size_t estimated_reduction =
                    late_move_reduction(child_depth, move_number, pv_node, noisy, false, history_score);
                const std::size_t estimated_lmr_depth =
                    child_depth > estimated_reduction ? child_depth - estimated_reduction : 0;
                const Score capture_value = noisy ? captured_piece_value(board, move) : 0;

                const bool noisy_see_bad = pruning_node && !pv_node && !in_check && best_move != 0 && noisy &&
                                           depth <= 8 && !see_ge(board, move, -static_cast<Score>(150 * depth));
                const bool quiet_see_bad =
                    pruning_node && !pv_node && !in_check && best_move != 0 && quiet_late_move && depth <= 8 &&
                    !see_ge(board, move, -static_cast<Score>(30 * estimated_lmr_depth * estimated_lmr_depth));

                Board::UndoState undo_state;
                if (!make_search_move(board, move, undo_state))
                {
                    continue;
                }

                ++searched;
                const bool gives_check = board.checkers() != 0;

                if (!gives_check && (noisy_see_bad || quiet_see_bad))
                {
                    undo_search_move(board);
                    continue;
                }

                if (pruning_node && !pv_node && !in_check && !gives_check && best_move != 0 && noisy && depth <= 7 &&
                    !is_promotion(move_flag(move)) && !is_mate_score(alpha))
                {
                    const Score futility_score =
                        evaluate_static() + static_cast<Score>(210 + 180 * estimated_lmr_depth) + capture_value;
                    if (futility_score <= alpha)
                    {
                        best_score = std::max(best_score, futility_score);
                        undo_search_move(board);
                        continue;
                    }
                }

                if (pruning_node && !pv_node && !in_check && !gives_check && best_move != 0 && quiet_late_move &&
                    depth <= 8)
                {
                    if (move_number >= futility_move_count(depth, is_improving()))
                    {
                        undo_search_move(board);
                        continue;
                    }

                    if (!is_mate_score(alpha))
                    {
                        const Score margin = quiet_futility_margin(estimated_lmr_depth, history_score, tt_move);
                        const Score futility_score = evaluate_static() + margin;
                        if (futility_score <= alpha)
                        {
                            best_score = std::max(best_score, futility_score);
                            undo_search_move(board);
                            continue;
                        }
                    }
                }

                if (!noisy && searched_quiet_count < searched_quiets.size())
                {
                    searched_quiets[searched_quiet_count] = move;
                    searched_quiet_piece_squares[searched_quiet_count] = current_piece_square;
                    ++searched_quiet_count;
                }

                std::vector<Move> child_pv;
                Score score = 0;
                if (ply + 1 < search_stack_.size())
                {
                    search_stack_[ply + 1].previous_move = move;
                    search_stack_[ply + 1].previous_piece_square = current_piece_square;
                }
                const std::size_t reduction =
                    pruning_node && !in_check && !gives_check
                        ? late_move_reduction(child_depth, move_number, pv_node, noisy, gives_check, history_score)
                        : 0;
                if (move_number == 1)
                {
                    score = -alpha_beta(board, child_depth, -beta, -alpha, ply + 1, child_pv, true, child_conservative);
                }
                else
                {
                    std::size_t search_depth = child_depth;
                    if (reduction != 0)
                    {
                        search_depth = child_depth - reduction;
                    }

                    score = -alpha_beta(board, search_depth, -alpha - 1, -alpha, ply + 1, child_pv, true,
                                        child_conservative);
                    if (reduction != 0 && score > alpha)
                    {
                        score = -alpha_beta(board, child_depth, -alpha - 1, -alpha, ply + 1, child_pv, true,
                                            child_conservative);
                    }
                    if (score > alpha && score < beta)
                    {
                        score =
                            -alpha_beta(board, child_depth, -beta, -alpha, ply + 1, child_pv, true, child_conservative);
                    }
                }

                undo_search_move(board);

                if (stopped())
                {
                    break;
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
                    record_quiet_cutoff(state_, move, depth, ply, previous_move, previous_piece_square,
                                        current_piece_square);
                    if (!noisy)
                    {
                        const int malus = -static_cast<int>(depth * depth);
                        for (std::size_t index = 0; index + 1 < searched_quiet_count; ++index)
                        {
                            update_quiet_history(state_, searched_quiets[index], malus, 0, previous_piece_square,
                                                 searched_quiet_piece_squares[index]);
                        }
                    }
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
                state_.table.store(board.key(), depth, score_to_tt(best_score, ply), bound, best_move,
                                   stack.static_eval_ready ? std::optional<Score>{stack.static_eval} : std::nullopt);
            }
            return best_score;
        }

        Score SearchWorker::quiescence(Board& board, Score alpha, Score beta, std::size_t ply, std::vector<Move>& pv)
        {
            if (stopped())
            {
                pv.clear();
                return evaluate(board);
            }

            if (board.is_draw())
            {
                pv.clear();
                return 0;
            }

            count_node();
            state_.selective_depth = std::max(state_.selective_depth, ply);
            pv.clear();
            const Score original_alpha = alpha;
            const bool pv_node = beta - alpha > 1;

            auto& stack = search_stack_[std::min(ply, search_stack_.size() - 1)];
            const Move previous_move = stack.previous_move;
            const std::size_t previous_piece_square = stack.previous_piece_square;
            stack = {};
            stack.previous_move = previous_move;
            stack.previous_piece_square = previous_piece_square;
            stack.in_check = board.checkers() != 0;
            const bool in_check = stack.in_check;
            std::optional<TranspositionEntry> tt_entry = state_.table.probe(board.key());
            if (tt_entry)
            {
                stack.tt_move = tt_entry->best_move;
                if (tt_entry->has_static_eval)
                {
                    stack.static_eval = tt_entry->static_eval;
                    stack.static_eval_ready = true;
                }
            }
            const Move tt_move = stack.tt_move;
            Score futility_base = -kInfiniteScore;
            auto evaluate_static = [&]() noexcept -> Score
            {
                if (!stack.static_eval_ready)
                {
                    stack.static_eval = evaluate(board);
                    stack.static_eval_ready = true;
                }
                return stack.static_eval;
            };

            if (!pv_node && tt_entry)
            {
                const Score tt_score = score_from_tt(tt_entry->score, ply);
                if (!pv_node && tt_entry->bound == Bound::Exact)
                {
                    return tt_score;
                }
                if (tt_entry->bound == Bound::Lower)
                {
                    alpha = std::max(alpha, tt_score);
                }
                else if (tt_entry->bound == Bound::Upper)
                {
                    beta = std::min(beta, tt_score);
                }

                if (!pv_node && alpha >= beta)
                {
                    return tt_score;
                }
            }

            if (!in_check || ply >= state_.max_quiescence_ply)
            {
                const Score stand_pat = evaluate_static();
                if (stand_pat >= beta)
                {
                    return beta;
                }
                alpha = std::max(alpha, stand_pat);
                futility_base = stand_pat + 250;

                if (ply >= state_.max_quiescence_ply)
                {
                    return alpha;
                }
            }

            Move best_move = 0;
            MovePicker picker{board, move_ordering(tt_move, state_, ply, previous_move, previous_piece_square)};
            const bool generated_any = picker.generated_any();
            for (Move move = picker.next(); move != 0; move = picker.next())
            {
                if (stopped())
                {
                    break;
                }

                if (!in_check && !is_noisy(move))
                {
                    continue;
                }
                if (!in_check && is_noisy(move) && !is_good_noisy(board, move))
                {
                    continue;
                }
                if (!in_check && !is_promotion(move_flag(move)) && futility_base != -kInfiniteScore)
                {
                    const Score futility_score = futility_base + captured_piece_value(board, move);
                    if (futility_score <= alpha)
                    {
                        continue;
                    }

                    if (!see_ge(board, move, alpha - futility_base))
                    {
                        continue;
                    }
                }
                const std::size_t current_piece_square =
                    piece_square_history_index(board.piece_on(move_from(move)), move_to(move));
                Board::UndoState undo_state;
                if (!make_search_move(board, move, undo_state))
                {
                    continue;
                }

                std::vector<Move> child_pv;
                if (ply + 1 < search_stack_.size())
                {
                    search_stack_[ply + 1].previous_move = move;
                    search_stack_[ply + 1].previous_piece_square = current_piece_square;
                }
                const Score score = -quiescence(board, -beta, -alpha, ply + 1, child_pv);
                undo_search_move(board);

                if (stopped())
                {
                    break;
                }

                if (score >= beta)
                {
                    if (!stopped())
                    {
                        state_.table.store(board.key(), 0, score_to_tt(beta, ply), Bound::Lower, move,
                                           stack.static_eval_ready ? std::optional<Score>{stack.static_eval}
                                                                   : std::nullopt);
                    }
                    return beta;
                }
                if (score > alpha)
                {
                    best_move = move;
                    update_pv(pv, move, child_pv);
                    alpha = score;
                }
            }

            if (!generated_any)
            {
                alpha = in_check ? -kMateScore + static_cast<Score>(ply) : alpha;
            }

            if (!stopped())
            {
                const Bound bound = alpha <= original_alpha ? Bound::Upper : Bound::Exact;
                state_.table.store(board.key(), 0, score_to_tt(alpha, ply), bound, best_move,
                                   stack.static_eval_ready ? std::optional<Score>{stack.static_eval} : std::nullopt);
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
            std::atomic_uint64_t shared_nodes{0};
            auto run_worker = [&](std::size_t worker_id)
            {
                if (worker_id >= worker_count)
                {
                    return;
                }

                SearchLimits worker_limits = limits;
                worker_limits.threads = 1;
                worker_limits.worker_id = worker_id;
                worker_limits.shared_nodes = &shared_nodes;
                worker_limits.thread_pool = nullptr;
                if (worker_id == 0 && limits.on_iteration)
                {
                    worker_limits.on_iteration = [&](const SearchIteration& iteration)
                    {
                        SearchIteration collective = iteration;
                        collective.nodes = shared_nodes.load(std::memory_order_relaxed);
                        limits.on_iteration(collective);
                    };
                }
                else if (worker_id != 0)
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

            SearchResult result = select_lazy_smp_result(worker_results);
            result.nodes = shared_nodes.load(std::memory_order_relaxed);
            return result;
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
