#include "engine.hpp"
#include "uci.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace
{

    TEST(UciTests, AcceptsPositionFenCommand)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1\n"
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

    TEST(UciTests, AppliesPositionMoves)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos moves e2e4 e7e5\ngo perft 1\nquit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("Nodes searched: 29"), std::string::npos);
    }

    TEST(UciTests, ReportsSearchBestMove)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos\ngo depth 2\nquit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("info depth 1"), std::string::npos);
        EXPECT_NE(output.str().find("info depth 2"), std::string::npos);
        EXPECT_NE(output.str().find("seldepth "), std::string::npos);
        EXPECT_NE(output.str().find("bestmove "), std::string::npos);
    }

    TEST(UciTests, ReportsUciOptions)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"uci\nquit\n"};
        std::ostringstream output;

        aurora::chess::run_uci_loop(engine, input, output);

        EXPECT_NE(output.str().find("id name Aurora"), std::string::npos);
        EXPECT_NE(output.str().find("option name Hash type spin"), std::string::npos);
        EXPECT_NE(output.str().find("option name Clear Hash type button"), std::string::npos);
        EXPECT_NE(output.str().find("option name Ponder type check"), std::string::npos);
        EXPECT_NE(output.str().find("uciok"), std::string::npos);
    }

    TEST(UciTests, AppliesSetOptionCommands)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"setoption name Hash value 32\nsetoption name Clear Hash\nisready\nquit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("readyok"), std::string::npos);
    }

    TEST(UciTests, NewGameResetsPosition)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos moves e2e4\nucinewgame\ngo perft 1\nquit\n"};
        std::ostringstream output;

        aurora::chess::run_uci_loop(engine, input, output);

        EXPECT_NE(output.str().find("Nodes searched: 20"), std::string::npos);
    }

    TEST(UciTests, AcceptsTimeStyleGoCommand)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos\ngo movetime 1\nquit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("bestmove "), std::string::npos);
    }

    TEST(UciTests, PrintsBoardAndEvaluation)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos\nd\neval\nquit\n"};
        std::ostringstream output;

        aurora::chess::run_uci_loop(engine, input, output);

        EXPECT_NE(output.str().find("Fen:"), std::string::npos);
        EXPECT_NE(output.str().find("info string eval "), std::string::npos);
    }

} // namespace
