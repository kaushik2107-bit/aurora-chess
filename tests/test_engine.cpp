#include "engine.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(EngineTests, ReportsBasicMetadata)
    {
        aurora::chess::Engine engine{"Aurora"};

        EXPECT_EQ(engine.name(), std::string_view{"Aurora"});
        EXPECT_EQ(engine.version(), "0.1.0");
    }

    TEST(EngineTests, ParsesTheInitialPosition)
    {
        aurora::chess::Engine engine{"Aurora"};
        engine.set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

        const auto &board = engine.board();
        EXPECT_EQ(board.side_to_move(), aurora::chess::Color::White);
        EXPECT_EQ(board.castling_rights(), aurora::chess::CastlingRights::All);
        EXPECT_EQ(board.piece_on(aurora::chess::Square::E1), aurora::chess::Piece::WhiteKing);
        EXPECT_EQ(board.piece_on(aurora::chess::Square::E8), aurora::chess::Piece::BlackKing);
    }

} // namespace
