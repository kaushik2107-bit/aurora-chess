#include "perft.hpp"

#include "helpers.hpp"
#include "movegen.hpp"

namespace aurora::chess
{
    namespace
    {

        [[nodiscard]] std::uint64_t perft_recursive(Board &board, std::size_t depth)
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

                if (!board.make_move(entry.move))
                {
                    continue;
                }

                nodes += leaf ? move_count(MoveGenerator{}.generate(board)) : perft_recursive(board, depth - 1);
                board.undo_move();
            }
            return nodes;
        }

    } // namespace

    std::uint64_t perft(const Board &board, std::size_t depth)
    {
        Board working = board;
        return perft_recursive(working, depth);
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

            Board working = board;
            if (!working.make_move(entry.move))
            {
                continue;
            }

            result.emplace_back(entry.move, depth <= 1 ? 1 : perft_recursive(working, depth - 1));
        }
        return result;
    }

} // namespace aurora::chess
