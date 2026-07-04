#include "engine.hpp"
#include "uci.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace
{

    TEST(UciTests, AcceptsPositionFenCommand)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{
            "position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1\n"
            "go perft 1\n"
            "quit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("Nodes searched: 48"), std::string::npos);
    }

    TEST(UciTests, AcceptsPositionStartposCommand)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos\ngo perft 1\nquit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("Nodes searched: 20"), std::string::npos);
    }

} // namespace
