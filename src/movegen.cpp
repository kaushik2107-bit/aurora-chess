#include "movegen.hpp"

#include "magic.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace aurora::chess
{
    namespace
    {

        constexpr std::array<int, 64> kSquareToFile = {
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
        };

        constexpr std::array<int, 64> kSquareToRank = {
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            3,
            3,
            3,
            3,
            3,
            3,
            3,
            3,
            4,
            4,
            4,
            4,
            4,
            4,
            4,
            4,
            5,
            5,
            5,
            5,
            5,
            5,
            5,
            5,
            6,
            6,
            6,
            6,
            6,
            6,
            6,
            6,
            7,
            7,
            7,
            7,
            7,
            7,
            7,
            7,
        };

        constexpr std::array<int, 64> kSquareToRow = {
            7,
            7,
            7,
            7,
            7,
            7,
            7,
            7,
            6,
            6,
            6,
            6,
            6,
            6,
            6,
            6,
            5,
            5,
            5,
            5,
            5,
            5,
            5,
            5,
            4,
            4,
            4,
            4,
            4,
            4,
            4,
            4,
            3,
            3,
            3,
            3,
            3,
            3,
            3,
            3,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        };

        constexpr Bitboard kFileA = 0x0101010101010101ull;
        constexpr Bitboard kFileH = 0x8080808080808080ull;

        constexpr bool on_board(int file, int rank) noexcept
        {
            return file >= 0 && file < 8 && rank >= 0 && rank < 8;
        }

        constexpr Square to_square(int file, int rank) noexcept
        {
            return static_cast<Square>(rank * 8 + file);
        }

        constexpr Square to_square(std::size_t index) noexcept
        {
            return static_cast<Square>(index);
        }

        [[nodiscard]] constexpr std::uint8_t square_index(Square square) noexcept
        {
            return static_cast<std::uint8_t>(square);
        }

        [[nodiscard]] Bitboard pawn_moves(Bitboard pawns, Color color) noexcept
        {
            if (color == Color::White)
            {
                return (pawns << 8) & ~0xFF00000000000000ull;
            }
            return (pawns >> 8) & ~0x00000000000000FFull;
        }

        [[nodiscard]] Bitboard pawn_captures(Bitboard pawns, Color color, Bitboard occupancy) noexcept
        {
            if (color == Color::White)
            {
                const Bitboard left = ((pawns & ~kFileA) << 7) & ~0xFF00000000000000ull;
                const Bitboard right = ((pawns & ~kFileH) << 9) & ~0xFF00000000000000ull;
                return (left | right) & occupancy;
            }
            const Bitboard left = ((pawns & ~kFileA) >> 9) & ~0x00000000000000FFull;
            const Bitboard right = ((pawns & ~kFileH) >> 7) & ~0x00000000000000FFull;
            return (left | right) & occupancy;
        }

        [[nodiscard]] Bitboard knight_moves(Square square) noexcept
        {
            const auto idx = square_index(square);
            Bitboard result = 0;
            const int file = kSquareToFile[idx];
            const int rank = kSquareToRank[idx];
            constexpr std::array<std::array<int, 2>, 8> deltas{{{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
            for (const auto &[df, dr] : deltas)
            {
                if (on_board(file + df, rank + dr))
                {
                    result |= bit(to_square(file + df, rank + dr));
                }
            }
            return result;
        }

        [[nodiscard]] Bitboard king_moves(Square square) noexcept
        {
            const auto idx = square_index(square);
            Bitboard result = 0;
            const int file = kSquareToFile[idx];
            const int rank = kSquareToRank[idx];
            constexpr std::array<std::array<int, 2>, 8> deltas{{{1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}}};
            for (const auto &[df, dr] : deltas)
            {
                if (on_board(file + df, rank + dr))
                {
                    result |= bit(to_square(file + df, rank + dr));
                }
            }
            return result;
        }

    } // namespace

    [[nodiscard]] std::uint32_t MoveGenerator::add_move(std::array<MoveEntry, 256> &out, std::size_t &count, Move move) noexcept
    {
        out[count++] = MoveEntry{move, 0};
        return static_cast<std::uint32_t>(count);
    }

    std::array<MoveEntry, 256> MoveGenerator::generate(const Board &board)
    {
        MagicBitboards::init();

        std::array<MoveEntry, 256> moves{};
        std::size_t count = 0;

        const auto us = board.side_to_move();

        generate_pawn_moves(board, moves, count, us);
        generate_knight_moves(board, moves, count, us);
        generate_bishop_moves(board, moves, count, us);
        generate_rook_moves(board, moves, count, us);
        generate_queen_moves(board, moves, count, us);
        generate_king_moves(board, moves, count, us);

        return moves;
    }

    void MoveGenerator::generate_pawn_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard pawns = board.piece_bb(PieceType::Pawn) & board.occupancy(us);
        const auto occupancy = board.all_occupancy();
        while (pawns)
        {
            const auto lsb = pawns & -pawns;
            const auto sq = static_cast<Square>(std::countr_zero(lsb));
            const auto target = us == Color::White ? static_cast<Square>(static_cast<int>(sq) + 8) : static_cast<Square>(static_cast<int>(sq) - 8);
            if (target != Square::NoSquare && board.is_empty(target))
            {
                static_cast<void>(add_move(moves, count, static_cast<Move>(static_cast<std::uint16_t>(sq) | (static_cast<std::uint16_t>(target) << 6))));
            }
            pawns &= pawns - 1;
        }
        (void)occupancy;
    }

    void MoveGenerator::generate_knight_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_knights = board.piece_bb(PieceType::Knight) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        while (our_knights)
        {
            const auto lsb = our_knights & -our_knights;
            const auto sq = static_cast<Square>(std::countr_zero(lsb));
            const auto moves_bb = knight_moves(sq) & ~friendly;
            Bitboard tmp = moves_bb;
            while (tmp)
            {
                const auto bit = tmp & -tmp;
                const auto to = static_cast<Square>(std::countr_zero(bit));
                static_cast<void>(add_move(moves, count, static_cast<Move>(static_cast<std::uint16_t>(sq) | (static_cast<std::uint16_t>(to) << 6))));
                tmp &= tmp - 1;
            }
            our_knights &= our_knights - 1;
        }
    }

    void MoveGenerator::generate_bishop_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_bishops = board.piece_bb(PieceType::Bishop) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        while (our_bishops)
        {
            const auto lsb = our_bishops & -our_bishops;
            const auto sq = static_cast<Square>(std::countr_zero(lsb));
            const auto moves_bb = MagicBitboards::bishop_moves(sq, board.all_occupancy()) & ~friendly;
            Bitboard tmp = moves_bb;
            while (tmp)
            {
                const auto bit = tmp & -tmp;
                const auto to = static_cast<Square>(std::countr_zero(bit));
                static_cast<void>(add_move(moves, count, static_cast<Move>(static_cast<std::uint16_t>(sq) | (static_cast<std::uint16_t>(to) << 6))));
                tmp &= tmp - 1;
            }
            our_bishops &= our_bishops - 1;
        }
    }

    void MoveGenerator::generate_rook_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_rooks = board.piece_bb(PieceType::Rook) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        while (our_rooks)
        {
            const auto lsb = our_rooks & -our_rooks;
            const auto sq = static_cast<Square>(std::countr_zero(lsb));
            const auto moves_bb = MagicBitboards::rook_moves(sq, board.all_occupancy()) & ~friendly;
            Bitboard tmp = moves_bb;
            while (tmp)
            {
                const auto bit = tmp & -tmp;
                const auto to = static_cast<Square>(std::countr_zero(bit));
                static_cast<void>(add_move(moves, count, static_cast<Move>(static_cast<std::uint16_t>(sq) | (static_cast<std::uint16_t>(to) << 6))));
                tmp &= tmp - 1;
            }
            our_rooks &= our_rooks - 1;
        }
    }

    void MoveGenerator::generate_queen_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_queens = board.piece_bb(PieceType::Queen) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        while (our_queens)
        {
            const auto lsb = our_queens & -our_queens;
            const auto sq = static_cast<Square>(std::countr_zero(lsb));
            const auto moves_bb = (MagicBitboards::bishop_moves(sq, board.all_occupancy()) | MagicBitboards::rook_moves(sq, board.all_occupancy())) & ~friendly;
            Bitboard tmp = moves_bb;
            while (tmp)
            {
                const auto bit = tmp & -tmp;
                const auto to = static_cast<Square>(std::countr_zero(bit));
                static_cast<void>(add_move(moves, count, static_cast<Move>(static_cast<std::uint16_t>(sq) | (static_cast<std::uint16_t>(to) << 6))));
                tmp &= tmp - 1;
            }
            our_queens &= our_queens - 1;
        }
    }

    void MoveGenerator::generate_king_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_kings = board.piece_bb(PieceType::King) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        while (our_kings)
        {
            const auto lsb = our_kings & -our_kings;
            const auto sq = static_cast<Square>(std::countr_zero(lsb));
            const auto moves_bb = king_moves(sq) & ~friendly;
            Bitboard tmp = moves_bb;
            while (tmp)
            {
                const auto bit = tmp & -tmp;
                const auto to = static_cast<Square>(std::countr_zero(bit));
                static_cast<void>(add_move(moves, count, static_cast<Move>(static_cast<std::uint16_t>(sq) | (static_cast<std::uint16_t>(to) << 6))));
                tmp &= tmp - 1;
            }
            our_kings &= our_kings - 1;
        }
    }

} // namespace aurora::chess
