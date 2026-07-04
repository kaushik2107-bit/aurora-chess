#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "board.hpp"

namespace aurora::chess
{

    struct SpeedStats
    {
        std::size_t depth{0};
        std::uint64_t nodes{0};
        std::uint64_t captures{0};
        std::uint64_t en_passant{0};
        std::uint64_t castles{0};
        std::uint64_t promotions{0};
        std::uint64_t checks{0};
        std::uint64_t checkmates{0};
    };

    [[nodiscard]] SpeedStats speed_stats_at_depth(const Board &board, std::size_t depth);
    [[nodiscard]] std::vector<SpeedStats> speed_stats(const Board &board, std::size_t max_depth);

} // namespace aurora::chess
