#pragma once

#include <chrono>
#include <optional>

#include "board.hpp"

namespace aurora::chess
{

    struct TimeControl
    {
        std::optional<int> white_time_ms;
        std::optional<int> black_time_ms;
        std::optional<int> white_increment_ms;
        std::optional<int> black_increment_ms;
        std::optional<int> moves_to_go;
        std::optional<int> move_time_ms;
        int move_overhead_ms{20};
    };

    struct TimePlan
    {
        bool active{false};
        std::chrono::milliseconds optimum{0};
        std::chrono::milliseconds maximum{0};
    };

    [[nodiscard]] TimePlan plan_time(const TimeControl& control, Color side_to_move, bool ponder) noexcept;

} // namespace aurora::chess
