#include "engine.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(MoveGenTests, GeneratesTheInitialPositionMoves)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

        const auto moves = engine.legal_moves();
        EXPECT_GT(moves.size(), 0u);
    }

} // namespace
