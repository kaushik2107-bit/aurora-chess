#include "see.hpp"

#include "attacks.hpp"
#include "helpers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace aurora::chess
{
    namespace
    {

        constexpr std::array<Score, static_cast<std::size_t>(PieceType::Count)> kSeePieceValues{
            100, 320, 330, 500, 900, 20000,
        };

        [[nodiscard]] constexpr Score piece_value(PieceType type) noexcept
        {
            return kSeePieceValues[static_cast<std::size_t>(type)];
        }

        [[nodiscard]] constexpr PieceType promotion_type(MoveFlag flag) noexcept
        {
            switch (flag)
            {
            case MoveFlag::KnightPromotion:
            case MoveFlag::KnightPromotionCapture:
                return PieceType::Knight;
            case MoveFlag::BishopPromotion:
            case MoveFlag::BishopPromotionCapture:
                return PieceType::Bishop;
            case MoveFlag::RookPromotion:
            case MoveFlag::RookPromotionCapture:
                return PieceType::Rook;
            case MoveFlag::QueenPromotion:
            case MoveFlag::QueenPromotionCapture:
                return PieceType::Queen;
            default:
                return PieceType::Count;
            }
        }

        [[nodiscard]] Score captured_value(const Board& board, Move move) noexcept
        {
            if (move_flag(move) == MoveFlag::EnPassant)
            {
                return piece_value(PieceType::Pawn);
            }

            const Piece captured = board.piece_on(move_to(move));
            return captured == Piece::None ? 0 : piece_value(piece_type(captured));
        }

        [[nodiscard]] Square en_passant_capture_square(Color side, Square to) noexcept
        {
            return static_cast<Square>(static_cast<int>(to) + (side == Color::White ? -8 : 8));
        }

        [[nodiscard]] Bitboard least_valuable_attacker(const Board& board, Bitboard attackers, Color side,
                                                       PieceType& type) noexcept
        {
            const Bitboard own = board.occupancy(side);
            for (PieceType candidate : {PieceType::Pawn, PieceType::Knight, PieceType::Bishop, PieceType::Rook,
                                        PieceType::Queen, PieceType::King})
            {
                const Bitboard pieces = attackers & own & board.piece_bb(candidate);
                if (pieces != 0)
                {
                    type = candidate;
                    return pieces & (0 - pieces);
                }
            }

            type = PieceType::Count;
            return 0;
        }

        [[nodiscard]] PieceType moved_piece_type_after(MoveFlag flag, PieceType moving) noexcept
        {
            const PieceType promoted = promotion_type(flag);
            return promoted == PieceType::Count ? moving : promoted;
        }

        [[nodiscard]] Score promotion_delta(MoveFlag flag) noexcept
        {
            const PieceType promoted = promotion_type(flag);
            return promoted == PieceType::Count ? 0 : piece_value(promoted) - piece_value(PieceType::Pawn);
        }

    } // namespace

    Score static_exchange_eval(const Board& board, Move move) noexcept
    {
        const Square from = move_from(move);
        const Square to = move_to(move);
        const MoveFlag flag = move_flag(move);
        const Piece moving = board.piece_on(from);
        if (moving == Piece::None)
        {
            return 0;
        }

        const Color us = board.side_to_move();
        const Color them = ~us;
        const PieceType moving_type = piece_type(moving);
        PieceType occupied_by = moved_piece_type_after(flag, moving_type);
        std::array<Score, 32> gain{};
        std::size_t depth = 0;

        gain[depth] = captured_value(board, move) + promotion_delta(flag);

        Bitboard occupancy = board.all_occupancy() & ~bit(from);
        if (flag == MoveFlag::EnPassant)
        {
            occupancy &= ~bit(en_passant_capture_square(us, to));
            occupancy |= bit(to);
        }

        Color side = them;
        while (depth + 1 < gain.size())
        {
            PieceType attacker_type = PieceType::Count;
            const Bitboard attackers = attackers_to(board, to, occupancy, side);
            const Bitboard attacker = least_valuable_attacker(board, attackers, side, attacker_type);
            if (attacker == 0)
            {
                break;
            }

            ++depth;
            gain[depth] = piece_value(occupied_by) - gain[depth - 1];
            occupancy &= ~attacker;
            occupied_by = attacker_type;
            side = ~side;
        }

        while (depth > 0)
        {
            gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
            --depth;
        }

        return gain[0];
    }

    bool see_ge(const Board& board, Move move, Score threshold) noexcept
    {
        return static_exchange_eval(board, move) >= threshold;
    }

} // namespace aurora::chess
