#include "engine.hpp"
#include "uci.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
        EXPECT_NE(output.str().find("option name Use NNUE type check"), std::string::npos);
        EXPECT_NE(output.str().find("option name Threads type spin"), std::string::npos);
        EXPECT_NE(output.str().find("Threads=1"), std::string::npos);
        EXPECT_NE(output.str().find("uciok"), std::string::npos);
    }

    TEST(UciTests, AppliesSetOptionCommands)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"setoption name Hash value 32\n"
                                 "setoption name Threads value 2\n"
                                 "setoption name Clear Hash\n"
                                 "isready\n"
                                 "quit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("readyok"), std::string::npos);
    }

    TEST(UciTests, StopReturnsBestMoveFromBackgroundSearch)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos\n"
                                 "setoption name Threads value 2\n"
                                 "go depth 8\n"
                                 "stop\n"
                                 "quit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));
        EXPECT_NE(output.str().find("bestmove "), std::string::npos);
        EXPECT_EQ(output.str().find("bestmove 0000"), std::string::npos);
    }

    TEST(UciTests, PonderWaitsForPonderhitBeforeBestMove)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"setoption name Ponder value true\n"
                                 "position startpos\n"
                                 "go ponder depth 1\n"
                                 "isready\n"
                                 "ponderhit\n"
                                 "quit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));

        const std::string text = output.str();
        const auto ready = text.find("readyok");
        const auto best = text.find("bestmove ");
        ASSERT_NE(ready, std::string::npos);
        ASSERT_NE(best, std::string::npos);
        EXPECT_LT(ready, best);
    }

    TEST(UciTests, PrintsPonderMoveWhenPonderIsEnabled)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"setoption name Ponder value true\n"
                                 "position startpos\n"
                                 "go depth 3\n"
                                 "quit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));

        const std::string text = output.str();
        EXPECT_NE(text.find("bestmove "), std::string::npos);
        EXPECT_NE(text.find(" ponder "), std::string::npos);
    }

    TEST(UciTests, PrintsOneInfoLinePerDepthWithThreads)
    {
        aurora::chess::Engine engine{"Aurora"};
        std::istringstream input{"position startpos\n"
                                 "setoption name Threads value 4\n"
                                 "go depth 4\n"
                                 "quit\n"};
        std::ostringstream output;

        EXPECT_NO_THROW(aurora::chess::run_uci_loop(engine, input, output));

        const std::string text = output.str();
        EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 5);
        EXPECT_NE(text.find("info depth 4"), std::string::npos);
        EXPECT_NE(text.find("bestmove "), std::string::npos);
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
