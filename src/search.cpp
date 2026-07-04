#include "search.hpp"

#include "helpers.hpp"
#include "movegen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace aurora::chess
{
    namespace
    {

        constexpr std::array<Score, static_cast<std::size_t>(PieceType::Count)> kMoveOrderPieceValues{
            100, 320, 330, 500, 900, 0,
        };

        [[nodiscard]] Score captured_piece_value(const Board& board, Move move) noexcept
        {
            const MoveFlag flag = move_flag(move);
            const Square to = move_to(move);
            if (flag == MoveFlag::EnPassant)
            {
                return kMoveOrderPieceValues[static_cast<std::size_t>(PieceType::Pawn)];
            }

            const Piece captured = board.piece_on(to);
            if (captured == Piece::None)
            {
                return 0;
            }
            return kMoveOrderPieceValues[static_cast<std::size_t>(piece_type(captured))];
        }

        [[nodiscard]] Score moving_piece_value(const Board& board, Move move) noexcept
        {
            const Piece moving = board.piece_on(move_from(move));
            if (moving == Piece::None)
            {
                return 0;
            }
            return kMoveOrderPieceValues[static_cast<std::size_t>(piece_type(moving))];
        }

        [[nodiscard]] Score promotion_bonus(MoveFlag flag) noexcept
        {
            switch (flag)
            {
            case MoveFlag::QueenPromotion:
            case MoveFlag::QueenPromotionCapture:
                return 900;
            case MoveFlag::RookPromotion:
            case MoveFlag::RookPromotionCapture:
                return 500;
            case MoveFlag::BishopPromotion:
            case MoveFlag::BishopPromotionCapture:
                return 330;
            case MoveFlag::KnightPromotion:
            case MoveFlag::KnightPromotionCapture:
                return 320;
            default:
                return 0;
            }
        }

        [[nodiscard]] Score move_order_score(const Board& board, Move move, Move tt_move) noexcept
        {
            if (move == tt_move)
            {
                return 30000;
            }

            const MoveFlag flag = move_flag(move);
            Score score = promotion_bonus(flag);
            if (is_capture(flag))
            {
                score += 10 * captured_piece_value(board, move) - moving_piece_value(board, move);
            }
            return score;
        }

        [[nodiscard]] bool is_noisy_move(Move move) noexcept
        {
            const MoveFlag flag = move_flag(move);
            return is_capture(flag) || is_promotion(flag);
        }

        void score_and_sort_moves(const Board& board, MoveList& moves, Move tt_move = 0)
        {
            for (auto& entry : moves)
            {
                entry.score =
                    static_cast<std::uint16_t>(std::max<Score>(0, move_order_score(board, entry.move, tt_move)));
            }

            std::sort(moves.begin(), moves.end(),
                      [](const MoveEntry& lhs, const MoveEntry& rhs) { return lhs.score > rhs.score; });
        }

        void update_pv(std::vector<Move>& pv, Move move, const std::vector<Move>& child_pv)
        {
            pv.clear();
            pv.reserve(child_pv.size() + 1);
            pv.push_back(move);
            pv.insert(pv.end(), child_pv.begin(), child_pv.end());
        }

    } // namespace

    struct SearchState
    {
        const Evaluator& evaluator;
        TranspositionTable& table;
        std::uint64_t nodes{0};
        std::size_t selective_depth{0};
        std::size_t max_quiescence_ply{0};
    };

    [[nodiscard]] Score quiescence(Board& board, Score alpha, Score beta, std::size_t ply, std::vector<Move>& pv,
                                   SearchState& state);

    [[nodiscard]] Score alpha_beta(Board& board, std::size_t depth, Score alpha, Score beta, std::size_t ply,
                                   std::vector<Move>& pv, SearchState& state)
    {
        if (depth == 0)
        {
            return quiescence(board, alpha, beta, ply, pv, state);
        }

        ++state.nodes;
        state.selective_depth = std::max(state.selective_depth, ply);
        const Score original_alpha = alpha;
        Move tt_move = 0;

        if (const auto* entry = state.table.probe(board.key()))
        {
            tt_move = entry->best_move;
            if (entry->depth >= depth)
            {
                if (entry->bound == Bound::Exact)
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

        auto moves = MoveGenerator{}.generate(board);
        if (moves.empty())
        {
            return board.checkers() != 0 ? -kMateScore + static_cast<Score>(ply) : 0;
        }

        score_and_sort_moves(board, moves, tt_move);

        Score best_score = -kInfiniteScore;
        Move best_move = 0;
        for (const auto& entry : moves)
        {
            if (!board.make_move(entry.move))
            {
                continue;
            }

            std::vector<Move> child_pv;
            const Score score = -alpha_beta(board, depth - 1, -beta, -alpha, ply + 1, child_pv, state);
            board.undo_move();

            best_score = std::max(best_score, score);
            if (score > alpha)
            {
                best_move = entry.move;
                update_pv(pv, entry.move, child_pv);
                alpha = score;
            }
            if (alpha >= beta)
            {
                break;
            }
        }

        const Bound bound = best_score <= original_alpha ? Bound::Upper
                            : best_score >= beta         ? Bound::Lower
                                                         : Bound::Exact;
        state.table.store(board.key(), depth, best_score, bound, best_move);
        return best_score;
    }

    [[nodiscard]] Score quiescence(Board& board, Score alpha, Score beta, std::size_t ply, std::vector<Move>& pv,
                                   SearchState& state)
    {
        ++state.nodes;
        state.selective_depth = std::max(state.selective_depth, ply);
        pv.clear();

        const bool in_check = board.checkers() != 0;
        if (!in_check || ply >= state.max_quiescence_ply)
        {
            const Score stand_pat = state.evaluator.evaluate(board);
            if (stand_pat >= beta)
            {
                return beta;
            }
            alpha = std::max(alpha, stand_pat);

            if (ply >= state.max_quiescence_ply)
            {
                return alpha;
            }
        }

        auto moves = MoveGenerator{}.generate(board);
        if (moves.empty())
        {
            return in_check ? -kMateScore + static_cast<Score>(ply) : alpha;
        }

        score_and_sort_moves(board, moves);

        for (const auto& entry : moves)
        {
            if (!in_check && !is_noisy_move(entry.move))
            {
                continue;
            }

            if (!board.make_move(entry.move))
            {
                continue;
            }

            std::vector<Move> child_pv;
            const Score score = -quiescence(board, -beta, -alpha, ply + 1, child_pv, state);
            board.undo_move();

            if (score >= beta)
            {
                return beta;
            }
            if (score > alpha)
            {
                update_pv(pv, entry.move, child_pv);
                alpha = score;
            }
        }

        return alpha;
    }

    SearchResult search(const Board& board, SearchLimits limits, TranspositionTable& table, const Evaluator& evaluator)
    {
        SearchResult result;
        Board working = board;
        const std::size_t max_depth = std::max<std::size_t>(1, limits.depth);
        result.iterations.reserve(max_depth);

        SearchState state{evaluator, table};

        for (std::size_t depth = 1; depth <= max_depth; ++depth)
        {
            state.max_quiescence_ply = depth + limits.quiescence_depth;
            Move best_move = 0;
            std::vector<Move> best_pv;
            Score best_score = -kInfiniteScore;
            Score alpha = -kInfiniteScore;

            const Move previous_best = result.best_move;
            auto moves = MoveGenerator{}.generate(working);
            score_and_sort_moves(working, moves, previous_best);

            for (const auto& entry : moves)
            {
                if (!working.make_move(entry.move))
                {
                    continue;
                }

                std::vector<Move> child_pv;
                const Score score = -alpha_beta(working, depth - 1, -kInfiniteScore, -alpha, 1, child_pv, state);
                working.undo_move();

                if (score > best_score)
                {
                    best_score = score;
                    best_move = entry.move;
                    update_pv(best_pv, entry.move, child_pv);
                    alpha = std::max(alpha, score);
                }
            }

            const SearchIteration iteration{
                best_move,   best_pv, moves.empty() ? (working.checkers() != 0 ? -kMateScore : 0) : best_score,
                state.nodes, depth,   state.selective_depth,
            };

            result.best_move = iteration.best_move;
            result.pv = iteration.pv;
            result.score = iteration.score;
            result.nodes = iteration.nodes;
            result.depth = iteration.depth;
            result.selective_depth = iteration.selective_depth;
            result.iterations.push_back(iteration);

            if (limits.on_iteration)
            {
                limits.on_iteration(iteration);
            }

            if (iteration.best_move != 0)
            {
                table.store(working.key(), depth, iteration.score, Bound::Exact, iteration.best_move);
            }
        }

        return result;
    }

} // namespace aurora::chess
