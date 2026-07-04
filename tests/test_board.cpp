#include "board.hpp"

#include <gtest/gtest.h>

namespace aurora::chess::test
{

    TEST(BoardTests, ParsesTheInitialPosition)
    {
        Board board{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"};

        EXPECT_EQ(board.side_to_move(), Color::White);
        EXPECT_EQ(board.castling_rights(), CastlingRights::All);
        EXPECT_EQ(board.piece_on(Square::E1), Piece::WhiteKing);
        EXPECT_EQ(board.piece_on(Square::E8), Piece::BlackKing);
        EXPECT_EQ(board.piece_on(Square::A1), Piece::WhiteRook);
        EXPECT_EQ(board.piece_on(Square::H8), Piece::BlackRook);
    }

    TEST(BoardTests, ParsesAnEmptyEnPassantSquare)
    {
        Board board{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b - - 0 1"};

        EXPECT_EQ(board.side_to_move(), Color::Black);
        EXPECT_EQ(board.en_passant_square(), Square::NoSquare);
        EXPECT_EQ(board.halfmove_clock(), 0u);
        EXPECT_EQ(board.fullmove_number(), 1u);
    }

    TEST(BoardTests, ReportsOccupancyAndPieceBitboards)
    {
        Board board{"8/8/8/8/8/8/8/4K3 w - - 0 1"};

        EXPECT_EQ(board.piece_bb(PieceType::King), bit(Square::E1));
        EXPECT_EQ(board.occupancy(Color::White), bit(Square::E1));
        EXPECT_EQ(board.all_occupancy(), bit(Square::E1));
        EXPECT_TRUE(board.is_empty(Square::D1));
    }

} // namespace aurora::chess::test
