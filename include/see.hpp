#pragma once

#include "evaluation.hpp"

namespace aurora::chess
{

    [[nodiscard]] Score static_exchange_eval(const Board& board, Move move) noexcept;
    [[nodiscard]] bool see_ge(const Board& board, Move move, Score threshold) noexcept;

} // namespace aurora::chess
