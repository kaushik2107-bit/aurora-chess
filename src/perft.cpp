#include "perft.hpp"

#include "movegen.hpp"

namespace aurora::chess
{
    namespace
    {

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

        [[nodiscard]] std::uint64_t perft_recursive(const Board &board, std::size_t depth)
        {
            if (depth == 0)
            {
                return 1;
            }

            const auto moves = MoveGenerator{}.generate(board);
            if (depth == 1)
            {
                return move_count(moves);
            }

            std::uint64_t nodes = 0;
            const bool leaf = depth == 2;
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

                nodes += leaf ? move_count(MoveGenerator{}.generate(next)) : perft_recursive(next, depth - 1);
            }
            return nodes;
        }

    } // namespace

    std::uint64_t perft(const Board &board, std::size_t depth)
    {
        return perft_recursive(board, depth);
    }

    std::vector<std::pair<Move, std::uint64_t>> perft_divide(const Board &board, std::size_t depth)
    {
        std::vector<std::pair<Move, std::uint64_t>> result;
        const auto moves = MoveGenerator{}.generate(board);
        result.reserve(move_count(moves));

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

            result.emplace_back(entry.move, depth <= 1 ? 1 : perft_recursive(next, depth - 1));
        }
        return result;
    }

} // namespace aurora::chess
