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

    TEST(BoardTests, TracksZobristKeyAcrossMakeAndUndo)
    {
        Board board;
        const Key initial_key = board.key();

        ASSERT_TRUE(board.make_move(make_move(Square::E2, Square::E4, MoveFlag::DoublePawnPush)));
        EXPECT_NE(board.key(), initial_key);

        ASSERT_TRUE(board.undo_move());
        EXPECT_EQ(board.key(), initial_key);
    }

    TEST(BoardTests, ZobristKeyIncludesSideToMove)
    {
        const Board white_to_move{"8/8/8/8/8/8/8/4K2k w - - 0 1"};
        const Board black_to_move{"8/8/8/8/8/8/8/4K2k b - - 0 1"};

        EXPECT_NE(white_to_move.key(), black_to_move.key());
    }

} // namespace aurora::chess::test
