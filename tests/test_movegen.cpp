#include "engine.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(MoveGenTests, GeneratesTheInitialPositionMoves)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

        const auto moves = engine.legal_moves();
        const auto count = std::count_if(moves.begin(), moves.end(), [](const auto &entry)
                                         { return entry.move != 0; });
        EXPECT_GT(count, 0);
    }

} // namespace
