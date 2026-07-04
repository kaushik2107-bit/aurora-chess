#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "board.hpp"

namespace aurora::chess
{

    [[nodiscard]] std::uint64_t perft(const Board &board, std::size_t depth);
    [[nodiscard]] std::vector<std::pair<Move, std::uint64_t>> perft_divide(const Board &board, std::size_t depth);

} // namespace aurora::chess
