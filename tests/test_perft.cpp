#include "engine.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(PerftTests, InitialPositionDepthOne)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        EXPECT_EQ(engine.perft(1), 20u);
    }

    TEST(PerftTests, InitialPositionDepthTwo)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        EXPECT_EQ(engine.perft(2), 400u);
    }

    TEST(PerftTests, InitialPositionDepthThree)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        EXPECT_EQ(engine.perft(3), 8902u);
    }

    TEST(PerftTests, KnownPositionDepthOne)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1");
        EXPECT_EQ(engine.perft(1), 48u);
    }

} // namespace
