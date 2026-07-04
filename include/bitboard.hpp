#pragma once

#include <cstddef>

#include "board.hpp"

namespace aurora::chess
{

    constexpr Bitboard kFileA = 0x0101010101010101ull;
    constexpr Bitboard kFileH = 0x8080808080808080ull;

    [[nodiscard]] constexpr int file_of(Square square) noexcept
    {
        return static_cast<int>(square) & 7;
    }

    [[nodiscard]] constexpr int rank_of(Square square) noexcept
    {
        return static_cast<int>(square) >> 3;
    }

    [[nodiscard]] constexpr bool on_board(int file, int rank) noexcept
    {
        return file >= 0 && file < 8 && rank >= 0 && rank < 8;
    }

    [[nodiscard]] constexpr Square to_square(int file, int rank) noexcept
    {
        return static_cast<Square>(rank * 8 + file);
    }

    [[nodiscard]] constexpr Square to_square(std::size_t index) noexcept
    {
        return static_cast<Square>(index);
    }

} // namespace aurora::chess
