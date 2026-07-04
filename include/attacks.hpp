#pragma once

#include "board.hpp"

namespace aurora::chess
{

    [[nodiscard]] Bitboard knight_attacks(Square square) noexcept;
    [[nodiscard]] Bitboard pawn_attacks(Square square, Color color) noexcept;
    [[nodiscard]] Bitboard bishop_attacks(Square square, Bitboard occupancy) noexcept;
    [[nodiscard]] Bitboard rook_attacks(Square square, Bitboard occupancy) noexcept;
    [[nodiscard]] Bitboard queen_attacks(Square square, Bitboard occupancy) noexcept;
    [[nodiscard]] Bitboard king_attacks(Square square) noexcept;
    [[nodiscard]] Bitboard attackers_to(const Board &board, Square square, Bitboard occupancy, Color by);
    [[nodiscard]] bool is_square_attacked(const Board &board, Square square, Color by);
    [[nodiscard]] bool is_in_check(const Board &board, Color color);

} // namespace aurora::chess
