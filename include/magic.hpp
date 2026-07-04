#pragma once

#include <array>
#include <cstdint>

#include "board.hpp"

namespace aurora::chess
{

    class MagicBitboards
    {
    public:
        static constexpr std::size_t kBoardSize = 64;

        static void init();
        static Bitboard rook_moves(Square square, Bitboard occupancy) noexcept;
        static Bitboard bishop_moves(Square square, Bitboard occupancy) noexcept;

    private:
        static bool initialized_;
        static std::array<Bitboard, kBoardSize> rook_masks_;
        static std::array<Bitboard, kBoardSize> bishop_masks_;
        static std::array<Bitboard, kBoardSize> rook_table_;
        static std::array<Bitboard, kBoardSize> bishop_table_;
    };

} // namespace aurora::chess
