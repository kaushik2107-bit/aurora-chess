#include "attacks.hpp"

#include "bitboard.hpp"
#include "magic.hpp"

#include <array>

namespace aurora::chess
{
    namespace
    {

        [[nodiscard]] Bitboard pawn_set_attacks(Bitboard pawns, Color color) noexcept
        {
            if (color == Color::White)
            {
                return ((pawns & ~kFileA) << 7) | ((pawns & ~kFileH) << 9);
            }
            return ((pawns & ~kFileA) >> 9) | ((pawns & ~kFileH) >> 7);
        }

    } // namespace

    Bitboard pawn_attacks(Square square, Color color) noexcept
    {
        return pawn_set_attacks(bit(square), color);
    }

    Bitboard knight_attacks(Square square) noexcept
    {
        Bitboard result = 0;
        // clang-format off
        constexpr std::array<std::array<int, 2>, 8> deltas{{
            { 1,  2},
            { 2,  1},
            { 2, -1},
            { 1, -2},
            {-1, -2},
            {-2, -1},
            {-2,  1},
            {-1,  2},
        }};
        // clang-format on
        const int file = file_of(square);
        const int rank = rank_of(square);
        for (const auto &[df, dr] : deltas)
        {
            if (on_board(file + df, rank + dr))
            {
                result |= bit(to_square(file + df, rank + dr));
            }
        }
        return result;
    }

    Bitboard king_attacks(Square square) noexcept
    {
        Bitboard result = 0;
        // clang-format off
        constexpr std::array<std::array<int, 2>, 8> deltas{{
            { 1,  1},
            { 1,  0},
            { 1, -1},
            { 0, -1},
            {-1, -1},
            {-1,  0},
            {-1,  1},
            { 0,  1},
        }};
        // clang-format on
        const int file = file_of(square);
        const int rank = rank_of(square);
        for (const auto &[df, dr] : deltas)
        {
            if (on_board(file + df, rank + dr))
            {
                result |= bit(to_square(file + df, rank + dr));
            }
        }
        return result;
    }

    Bitboard bishop_attacks(Square square, Bitboard occupancy) noexcept
    {
        return MagicBitboards::bishop_moves(square, occupancy);
    }

    Bitboard rook_attacks(Square square, Bitboard occupancy) noexcept
    {
        return MagicBitboards::rook_moves(square, occupancy);
    }

    Bitboard queen_attacks(Square square, Bitboard occupancy) noexcept
    {
        return bishop_attacks(square, occupancy) | rook_attacks(square, occupancy);
    }

    bool is_square_attacked(const Board &board, Square square, Color by)
    {
        const Bitboard target = bit(square);
        const Bitboard by_occupancy = board.occupancy(by);

        if ((pawn_set_attacks(board.piece_bb(PieceType::Pawn) & by_occupancy, by) & target) != 0)
        {
            return true;
        }

        Bitboard knights = board.piece_bb(PieceType::Knight) & by_occupancy;
        while (knights)
        {
            const auto from = static_cast<Square>(lsb_index(knights));
            if ((knight_attacks(from) & target) != 0)
            {
                return true;
            }
            knights &= knights - 1;
        }

        const Bitboard bishops_and_queens = (board.piece_bb(PieceType::Bishop) | board.piece_bb(PieceType::Queen)) & by_occupancy;
        if ((bishop_attacks(square, board.all_occupancy()) & bishops_and_queens) != 0)
        {
            return true;
        }

        const Bitboard rooks_and_queens = (board.piece_bb(PieceType::Rook) | board.piece_bb(PieceType::Queen)) & by_occupancy;
        if ((rook_attacks(square, board.all_occupancy()) & rooks_and_queens) != 0)
        {
            return true;
        }

        const Bitboard kings = board.piece_bb(PieceType::King) & by_occupancy;
        return kings != 0 && (king_attacks(static_cast<Square>(lsb_index(kings))) & target) != 0;
    }

    bool is_in_check(const Board &board, Color color)
    {
        const Bitboard king = board.piece_bb(PieceType::King) & board.occupancy(color);
        return king != 0 && is_square_attacked(board, static_cast<Square>(lsb_index(king)), ~color);
    }

} // namespace aurora::chess
