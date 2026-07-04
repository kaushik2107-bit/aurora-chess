#include "speed.hpp"

#include "attacks.hpp"
#include "movegen.hpp"

namespace aurora::chess
{
    namespace
    {

        [[nodiscard]] bool is_capture(MoveFlag flag) noexcept
        {
            return flag == MoveFlag::Capture || flag == MoveFlag::EnPassant ||
                   flag == MoveFlag::KnightPromotionCapture || flag == MoveFlag::BishopPromotionCapture ||
                   flag == MoveFlag::RookPromotionCapture || flag == MoveFlag::QueenPromotionCapture;
        }

        [[nodiscard]] bool is_castle(MoveFlag flag) noexcept
        {
            return flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle;
        }

        [[nodiscard]] std::size_t move_count(const std::array<MoveEntry, 256> &moves)
        {
            std::size_t count = 0;
            for (const auto &entry : moves)
            {
                if (entry.move != 0)
                {
                    ++count;
                }
            }
            return count;
        }

        void collect_speed_stats(const Board &board, std::size_t depth, SpeedStats &stats)
        {
            const auto moves = MoveGenerator{}.generate(board);
            for (const auto &entry : moves)
            {
                if (entry.move == 0)
                {
                    continue;
                }

                Board next = board;
                if (!next.make_move(entry.move))
                {
                    continue;
                }

                if (depth > 1)
                {
                    collect_speed_stats(next, depth - 1, stats);
                    continue;
                }

                const auto flag = move_flag(entry.move);
                ++stats.nodes;
                stats.captures += is_capture(flag) ? 1 : 0;
                stats.en_passant += flag == MoveFlag::EnPassant ? 1 : 0;
                stats.castles += is_castle(flag) ? 1 : 0;
                stats.promotions += is_promotion(flag) ? 1 : 0;

                if (is_in_check(next, next.side_to_move()))
                {
                    ++stats.checks;
                    stats.checkmates += move_count(MoveGenerator{}.generate(next)) == 0 ? 1 : 0;
                }
            }
        }

    } // namespace

    std::vector<SpeedStats> speed_stats(const Board &board, std::size_t max_depth)
    {
        std::vector<SpeedStats> result;
        result.reserve(max_depth);

        for (std::size_t depth = 1; depth <= max_depth; ++depth)
        {
            SpeedStats stats;
            stats.depth = depth;
            collect_speed_stats(board, depth, stats);
            result.push_back(stats);
        }

        return result;
    }

} // namespace aurora::chess
