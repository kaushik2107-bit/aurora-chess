#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "board.hpp"

namespace aurora::chess
{

    struct MoveEntry
    {
        Move move{0};
        std::uint16_t score{0};
    };

    class MoveList
    {
    public:
        static constexpr std::size_t kMaxMoves = 256;

        void push(Move move, std::uint16_t score = 0) noexcept;
        [[nodiscard]] MoveEntry pop_back() noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;

        [[nodiscard]] MoveEntry* begin() noexcept;
        [[nodiscard]] MoveEntry* end() noexcept;
        [[nodiscard]] const MoveEntry* begin() const noexcept;
        [[nodiscard]] const MoveEntry* end() const noexcept;

    private:
        std::array<MoveEntry, kMaxMoves> moves_{};
        std::size_t count_{0};
    };

    class MoveGenerator
    {
    public:
        MoveList generate(const Board& board);
    };

} // namespace aurora::chess
