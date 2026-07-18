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

    TEST(BoardTests, NullMoveCanBeUndone)
    {
        Board board;
        const std::string fen = board.fen();
        const Key key = board.key();

        ASSERT_TRUE(board.make_null_move());
        EXPECT_EQ(board.side_to_move(), Color::Black);

        ASSERT_TRUE(board.undo_move());
        EXPECT_EQ(board.fen(), fen);
        EXPECT_EQ(board.key(), key);
    }

    TEST(BoardTests, SupportsGamesBeyondThePreviousHistoryLimit)
    {
        Board board{"6nk/8/8/8/8/8/8/KN6 w - - 0 1"};
        const std::string initial_fen = board.fen();
        const Key initial_key = board.key();

        constexpr std::array moves{
            make_move(Square::B1, Square::C3),
            make_move(Square::G8, Square::F6),
            make_move(Square::C3, Square::B1),
            make_move(Square::F6, Square::G8),
        };
        constexpr std::size_t move_count = 300;
        for (std::size_t ply = 0; ply < move_count; ++ply)
        {
            ASSERT_TRUE(board.make_move(moves[ply % moves.size()])) << "ply " << ply;
        }

        for (std::size_t ply = 0; ply < move_count; ++ply)
        {
            ASSERT_TRUE(board.undo_move()) << "undo " << ply;
        }
        EXPECT_EQ(board.fen(), initial_fen);
        EXPECT_EQ(board.key(), initial_key);
    }

    TEST(BoardTests, DetectsFiftyMoveRuleDraw)
    {
        const Board board{"4k2r/8/8/8/8/8/8/R3K3 w - - 100 1"};

        EXPECT_TRUE(board.is_fifty_move_rule_draw());
        EXPECT_EQ(board.draw_reason(), DrawReason::FiftyMoveRule);
        EXPECT_TRUE(board.is_draw());
    }

    TEST(BoardTests, CountsThreefoldRepetition)
    {
        Board board{"6nk/8/8/8/8/8/8/KN6 w - - 0 1"};

        EXPECT_EQ(board.repetition_count(), 1u);
        EXPECT_FALSE(board.is_repetition());

        ASSERT_TRUE(board.make_move(make_move(Square::B1, Square::C3)));
        ASSERT_TRUE(board.make_move(make_move(Square::G8, Square::F6)));
        ASSERT_TRUE(board.make_move(make_move(Square::C3, Square::B1)));
        ASSERT_TRUE(board.make_move(make_move(Square::F6, Square::G8)));

        EXPECT_EQ(board.repetition_count(), 2u);
        EXPECT_FALSE(board.is_repetition());

        ASSERT_TRUE(board.make_move(make_move(Square::B1, Square::C3)));
        ASSERT_TRUE(board.make_move(make_move(Square::G8, Square::F6)));
        ASSERT_TRUE(board.make_move(make_move(Square::C3, Square::B1)));
        ASSERT_TRUE(board.make_move(make_move(Square::F6, Square::G8)));

        EXPECT_EQ(board.repetition_count(), 3u);
        EXPECT_TRUE(board.is_repetition());
        EXPECT_EQ(board.draw_reason(), DrawReason::Repetition);
    }

    TEST(BoardTests, DetectsInsufficientMaterial)
    {
        EXPECT_TRUE(Board{"8/8/8/8/8/8/8/K6k w - - 0 1"}.has_insufficient_material());
        EXPECT_TRUE(Board{"8/8/8/8/8/8/8/KB5k w - - 0 1"}.has_insufficient_material());
        EXPECT_TRUE(Board{"8/8/8/8/8/8/8/KN5k w - - 0 1"}.has_insufficient_material());
        EXPECT_TRUE(Board{"7k/8/7b/8/8/8/8/K1B5 w - - 0 1"}.has_insufficient_material());

        EXPECT_FALSE(Board{"7k/8/6b1/8/8/8/8/K1B5 w - - 0 1"}.has_insufficient_material());
        EXPECT_FALSE(Board{"7k/8/8/8/8/8/8/KNN5 w - - 0 1"}.has_insufficient_material());
        EXPECT_FALSE(Board{"7k/8/8/8/8/8/8/KR6 w - - 0 1"}.has_insufficient_material());
    }

} // namespace aurora::chess::test
