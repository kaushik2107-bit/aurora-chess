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
    };

} // namespace aurora::chess
