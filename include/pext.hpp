#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "board.hpp"

namespace aurora::chess
{

    class SlidingAttacks
    {
    public:
        static constexpr std::size_t kSquareCount = 64;

        static void init();
        static Bitboard rook_attacks(Square square, Bitboard occupancy) noexcept;
        static Bitboard bishop_attacks(Square square, Bitboard occupancy) noexcept;

    private:
        static bool is_initialized_;
        static std::array<Bitboard, kSquareCount> rook_occupancy_masks_;
        static std::array<Bitboard, kSquareCount> bishop_occupancy_masks_;
        static std::array<std::vector<Bitboard>, kSquareCount> rook_attack_tables_;
        static std::array<std::vector<Bitboard>, kSquareCount> bishop_attack_tables_;
    };

} // namespace aurora::chess
