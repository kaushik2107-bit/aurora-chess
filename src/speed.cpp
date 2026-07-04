#include "speed.hpp"

#include "attacks.hpp"
#include "helpers.hpp"
#include "movegen.hpp"

namespace aurora::chess
{
    namespace
    {

        void collect_speed_stats(Board &board, std::size_t depth, SpeedStats &stats)
        {
            const auto moves = MoveGenerator{}.generate(board);
            for (const auto &entry : moves)
            {
                if (!board.make_move(entry.move))
                {
                    continue;
                }

                if (depth > 1)
                {
                    collect_speed_stats(board, depth - 1, stats);
                    board.undo_move();
                    continue;
                }

                const auto flag = move_flag(entry.move);
                ++stats.nodes;
                stats.captures += is_capture(flag) ? 1 : 0;
                stats.en_passant += flag == MoveFlag::EnPassant ? 1 : 0;
                stats.castles += is_castle(flag) ? 1 : 0;
                stats.promotions += is_promotion(flag) ? 1 : 0;

                if (is_in_check(board, board.side_to_move()))
                {
                    ++stats.checks;
                    stats.checkmates += MoveGenerator{}.generate(board).empty() ? 1 : 0;
                }
                board.undo_move();
            }
        }

    } // namespace

    SpeedStats speed_stats_at_depth(const Board &board, std::size_t depth)
    {
        SpeedStats stats;
        stats.depth = depth;
        Board working = board;
        collect_speed_stats(working, depth, stats);
        return stats;
    }

    std::vector<SpeedStats> speed_stats(const Board &board, std::size_t max_depth)
    {
        std::vector<SpeedStats> result;
        result.reserve(max_depth);

        for (std::size_t depth = 1; depth <= max_depth; ++depth)
        {
            result.push_back(speed_stats_at_depth(board, depth));
        }

        return result;
    }

} // namespace aurora::chess
