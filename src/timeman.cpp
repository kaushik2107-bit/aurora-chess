#include "timeman.hpp"

#include <algorithm>
#include <chrono>

namespace aurora::chess
{
    namespace
    {

        constexpr int kDefaultMovesToGo = 30;

        [[nodiscard]] int clamp_positive(int value) noexcept
        {
            return std::max(1, value);
        }

        [[nodiscard]] std::optional<int> time_for_side(const TimeControl& control, Color side_to_move) noexcept
        {
            return side_to_move == Color::White ? control.white_time_ms : control.black_time_ms;
        }

        [[nodiscard]] int increment_for_side(const TimeControl& control, Color side_to_move) noexcept
        {
            const auto increment =
                side_to_move == Color::White ? control.white_increment_ms : control.black_increment_ms;
            return std::max(0, increment.value_or(0));
        }

    } // namespace

    TimePlan plan_time(const TimeControl& control, Color side_to_move, bool ponder) noexcept
    {
        if (control.move_time_ms)
        {
            const auto fixed = std::chrono::milliseconds{clamp_positive(*control.move_time_ms)};
            return TimePlan{true, fixed, fixed};
        }

        const auto remaining_time = time_for_side(control, side_to_move);
        if (!remaining_time || *remaining_time <= 0)
        {
            return {};
        }

        const int move_overhead = std::clamp(control.move_overhead_ms, 0, 5'000);
        const int available = clamp_positive(*remaining_time - move_overhead);
        const int increment = increment_for_side(control, side_to_move);
        const int moves_to_go = control.moves_to_go ? std::clamp(*control.moves_to_go, 1, 50) : kDefaultMovesToGo;

        int optimum = available / moves_to_go + (3 * increment) / 4;
        int maximum = 0;
        if (control.moves_to_go)
        {
            maximum = std::min((available * 8) / 10, std::max(optimum * 3, optimum + increment));
        }
        else
        {
            maximum = std::min((available * 8) / 10, std::max(optimum * 5, optimum + 2 * increment));
        }

        optimum = std::clamp(optimum, 1, available);
        maximum = std::clamp(std::max(maximum, optimum), optimum, available);

        if (ponder)
        {
            optimum = std::min(maximum, optimum + optimum / 4);
        }

        return TimePlan{true, std::chrono::milliseconds{optimum}, std::chrono::milliseconds{maximum}};
    }

} // namespace aurora::chess
