#include "movepick.hpp"

#include "attacks.hpp"
#include "helpers.hpp"
#include "see.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace aurora::chess
{
    namespace
    {

        constexpr std::array<Score, static_cast<std::size_t>(PieceType::Count)> kPieceValues{
            100, 320, 330, 500, 900, 0,
        };
        constexpr Score kMaxMoveScore = 65535;

        [[nodiscard]] Score captured_piece_value(const Board& board, Move move) noexcept
        {
            if (move_flag(move) == MoveFlag::EnPassant)
            {
                return kPieceValues[static_cast<std::size_t>(PieceType::Pawn)];
            }

            const Piece captured = board.piece_on(move_to(move));
            return captured == Piece::None ? 0 : kPieceValues[static_cast<std::size_t>(piece_type(captured))];
        }

        [[nodiscard]] Score moving_piece_value(const Board& board, Move move) noexcept
        {
            const Piece moving = board.piece_on(move_from(move));
            return moving == Piece::None ? 0 : kPieceValues[static_cast<std::size_t>(piece_type(moving))];
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

        [[nodiscard]] PieceType promotion_piece_type(MoveFlag flag) noexcept
        {
            switch (flag)
            {
            case MoveFlag::QueenPromotion:
            case MoveFlag::QueenPromotionCapture:
                return PieceType::Queen;
            case MoveFlag::RookPromotion:
            case MoveFlag::RookPromotionCapture:
                return PieceType::Rook;
            case MoveFlag::BishopPromotion:
            case MoveFlag::BishopPromotionCapture:
                return PieceType::Bishop;
            case MoveFlag::KnightPromotion:
            case MoveFlag::KnightPromotionCapture:
                return PieceType::Knight;
            default:
                return PieceType::Count;
            }
        }

        [[nodiscard]] bool attacks_with_piece(PieceType type, Square from, Square target, Bitboard occupancy) noexcept
        {
            const Bitboard target_bb = bit(target);
            switch (type)
            {
            case PieceType::Pawn:
                return false;
            case PieceType::Knight:
                return (knight_attacks(from) & target_bb) != 0;
            case PieceType::Bishop:
                return (bishop_attacks(from, occupancy) & target_bb) != 0;
            case PieceType::Rook:
                return (rook_attacks(from, occupancy) & target_bb) != 0;
            case PieceType::Queen:
                return (queen_attacks(from, occupancy) & target_bb) != 0;
            case PieceType::King:
                return (king_attacks(from) & target_bb) != 0;
            default:
                return false;
            }
        }

        [[nodiscard]] bool gives_check(const Board& board, Move move) noexcept
        {
            const Piece moving = board.piece_on(move_from(move));
            if (moving == Piece::None)
            {
                return false;
            }

            const Color us = board.side_to_move();
            const Square them_king = board.king_square(~us);
            if (them_king == Square::NoSquare)
            {
                return false;
            }

            const Square from = move_from(move);
            const Square to = move_to(move);
            const MoveFlag flag = move_flag(move);
            Bitboard occupancy_after = (board.all_occupancy() & ~bit(from)) | bit(to);
            if (flag == MoveFlag::EnPassant)
            {
                const auto captured_square = static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8));
                occupancy_after &= ~bit(captured_square);
            }

            const PieceType direct_type = is_promotion(flag) ? promotion_piece_type(flag) : piece_type(moving);
            if (direct_type == PieceType::Pawn)
            {
                if ((pawn_attacks(to, us) & bit(them_king)) != 0)
                {
                    return true;
                }
            }
            else if (attacks_with_piece(direct_type, to, them_king, occupancy_after))
            {
                return true;
            }

            return attackers_to(board, them_king, occupancy_after, us) != 0;
        }

        [[nodiscard]] bool same_move(Move lhs, Move rhs) noexcept
        {
            return lhs != 0 && lhs == rhs;
        }

        [[nodiscard]] std::uint16_t move_score(Score score) noexcept
        {
            return static_cast<std::uint16_t>(std::clamp(score, Score{0}, kMaxMoveScore));
        }

        [[nodiscard]] int continuation_score(const Board& board, Move move, const MoveOrdering& ordering) noexcept
        {
            if (ordering.continuation_history == nullptr || ordering.previous_piece_square == kNoPieceSquareHistory)
            {
                return 0;
            }

            const std::size_t current_piece_square =
                piece_square_history_index(board.piece_on(move_from(move)), move_to(move));
            if (current_piece_square == kNoPieceSquareHistory)
            {
                return 0;
            }

            const std::size_t index =
                ordering.previous_piece_square * kPieceSquareHistoryBuckets + current_piece_square;
            return (*ordering.continuation_history)[index];
        }

        void sort_by_score(MoveList& moves)
        {
            std::sort(moves.begin(), moves.end(),
                      [](const MoveEntry& lhs, const MoveEntry& rhs) { return lhs.score < rhs.score; });
        }

    } // namespace

    bool is_noisy(Move move) noexcept
    {
        const MoveFlag flag = move_flag(move);
        return is_capture(flag) || is_promotion(flag);
    }

    bool is_good_noisy(const Board& board, Move move) noexcept
    {
        const MoveFlag flag = move_flag(move);
        if (is_capture(flag))
        {
            return see_ge(board, move, 0);
        }

        return is_promotion(flag);
    }

    MovePicker::MovePicker(const Board& board, MoveOrdering ordering) : board_(board), ordering_(ordering)
    {
        moves_ = MoveGenerator{}.generate(board_);
        generated_any_ = !moves_.empty();
        score_moves();
    }

    bool MovePicker::generated_any() const noexcept
    {
        return generated_any_;
    }

    bool MovePicker::contains(Move move) const noexcept
    {
        if (move == 0)
        {
            return false;
        }

        for (const auto& entry : moves_)
        {
            if (entry.move == move)
            {
                return true;
            }
        }

        return false;
    }

    Move MovePicker::next_from_scored(MoveList& moves)
    {
        if (moves.empty())
        {
            return 0;
        }

        return moves.pop_back().move;
    }

    void MovePicker::score_moves()
    {
        for (const auto& entry : moves_)
        {
            const Move move = entry.move;
            if (same_move(move, ordering_.tt_move))
            {
                continue;
            }

            if (same_move(move, ordering_.killers[0]) || same_move(move, ordering_.killers[1]))
            {
                continue;
            }

            if (same_move(move, ordering_.counter_move))
            {
                continue;
            }

            if (is_noisy(move))
            {
                const Score score = promotion_bonus(move_flag(move)) + 10 * captured_piece_value(board_, move) -
                                    moving_piece_value(board_, move);
                (is_good_noisy(board_, move) ? good_noisy_ : bad_noisy_).push(move, move_score(score));
            }
            else
            {
                Score score = ordering_.history == nullptr ? 0 : (*ordering_.history)[static_cast<std::size_t>(move)];
                score += continuation_score(board_, move, ordering_);
                if (gives_check(board_, move) && see_ge(board_, move, -75))
                {
                    score += 12'000;
                }
                quiets_.push(move, move_score(score));
            }
        }

        sort_by_score(good_noisy_);
        sort_by_score(bad_noisy_);
        sort_by_score(quiets_);
    }

    Move MovePicker::next()
    {
        while (stage_ != Stage::Done)
        {
            switch (stage_)
            {
            case Stage::TtMove:
                stage_ = Stage::GoodNoisy;
                if (contains(ordering_.tt_move))
                {
                    return ordering_.tt_move;
                }
                break;
            case Stage::GoodNoisy:
                if (const Move move = next_from_scored(good_noisy_); move != 0)
                {
                    return move;
                }
                stage_ = Stage::Killers;
                break;
            case Stage::Killers:
                while (killer_index_ < ordering_.killers.size())
                {
                    const Move move = ordering_.killers[killer_index_++];
                    const bool duplicate_killer = killer_index_ > 1 && move == ordering_.killers[0];
                    if (!duplicate_killer && move != ordering_.tt_move && contains(move))
                    {
                        return move;
                    }
                }
                stage_ = Stage::CounterMove;
                break;
            case Stage::CounterMove:
                stage_ = Stage::Quiets;
                if (ordering_.counter_move != ordering_.tt_move && ordering_.counter_move != ordering_.killers[0] &&
                    ordering_.counter_move != ordering_.killers[1] && !is_noisy(ordering_.counter_move) &&
                    contains(ordering_.counter_move))
                {
                    return ordering_.counter_move;
                }
                break;
            case Stage::Quiets:
                if (const Move move = next_from_scored(quiets_); move != 0)
                {
                    return move;
                }
                stage_ = Stage::BadNoisy;
                break;
            case Stage::BadNoisy:
                if (const Move move = next_from_scored(bad_noisy_); move != 0)
                {
                    return move;
                }
                stage_ = Stage::Done;
                break;
            case Stage::Done:
                break;
            }
        }

        return 0;
    }

} // namespace aurora::chess
