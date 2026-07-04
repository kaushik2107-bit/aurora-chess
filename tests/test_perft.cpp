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

    TEST(PerftTests, InitialPositionDepthFour)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        EXPECT_EQ(engine.perft(4), 197281u);
    }

    TEST(PerftTests, KnownPositionDepthOne)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        EXPECT_EQ(engine.perft(1), 48u);
    }

    TEST(PerftTests, KnownPositionDepthTwo)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        EXPECT_EQ(engine.perft(2), 2039u);
    }

    TEST(PerftTests, KnownPositionDepthThree)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        EXPECT_EQ(engine.perft(3), 97862u);
    }

    TEST(PerftTests, KnownPositionBlackToMoveDepthOne)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1");
        EXPECT_EQ(engine.perft(1), 43u);
    }

    TEST(PerftTests, TrickyEndgameDepthFour)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        EXPECT_EQ(engine.perft(4), 43238u);
    }

} // namespace
