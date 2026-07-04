#pragma once

#include "board.hpp"

namespace aurora::chess
{

    namespace zobrist
    {

        [[nodiscard]] Key hash(const Board &board) noexcept;

    } // namespace zobrist

} // namespace aurora::chess
