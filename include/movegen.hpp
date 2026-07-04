#pragma once

#include <array>
#include <cstdint>

#include "board.hpp"

namespace aurora::chess
{

    struct MoveEntry
    {
        Move move{0};
        std::uint16_t score{0};
    };

    class MoveGenerator
    {
    public:
        std::array<MoveEntry, 256> generate(const Board &board);

    private:
        void generate_pawn_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const;
        void generate_knight_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const;
        void generate_bishop_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const;
        void generate_rook_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const;
        void generate_queen_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const;
        void generate_king_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const;

        static std::uint32_t add_move(std::array<MoveEntry, 256> &out, std::size_t &count, Move move) noexcept;
    };

} // namespace aurora::chess
