#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace aurora::chess
{

    using Bitboard = std::uint64_t;
    using Key = std::uint64_t;
    using Move = std::uint16_t;

    constexpr std::string_view kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    enum class Color : std::uint8_t
    {
        White = 0,
        Black = 1,
    };

    enum class PieceType : std::uint8_t
    {
        Pawn = 0,
        Knight = 1,
        Bishop = 2,
        Rook = 3,
        Queen = 4,
        King = 5,
        Count = 6,
    };

    enum class Piece : std::uint8_t
    {
        None = 0,
        WhitePawn,
        WhiteKnight,
        WhiteBishop,
        WhiteRook,
        WhiteQueen,
        WhiteKing,
        BlackPawn,
        BlackKnight,
        BlackBishop,
        BlackRook,
        BlackQueen,
        BlackKing,
    };

    enum class Square : std::uint8_t
    {
        A1 = 0,
        B1 = 1,
        C1 = 2,
        D1 = 3,
        E1 = 4,
        F1 = 5,
        G1 = 6,
        H1 = 7,
        A2 = 8,
        B2 = 9,
        C2 = 10,
        D2 = 11,
        E2 = 12,
        F2 = 13,
        G2 = 14,
        H2 = 15,
        A3 = 16,
        B3 = 17,
        C3 = 18,
        D3 = 19,
        E3 = 20,
        F3 = 21,
        G3 = 22,
        H3 = 23,
        A4 = 24,
        B4 = 25,
        C4 = 26,
        D4 = 27,
        E4 = 28,
        F4 = 29,
        G4 = 30,
        H4 = 31,
        A5 = 32,
        B5 = 33,
        C5 = 34,
        D5 = 35,
        E5 = 36,
        F5 = 37,
        G5 = 38,
        H5 = 39,
        A6 = 40,
        B6 = 41,
        C6 = 42,
        D6 = 43,
        E6 = 44,
        F6 = 45,
        G6 = 46,
        H6 = 47,
        A7 = 48,
        B7 = 49,
        C7 = 50,
        D7 = 51,
        E7 = 52,
        F7 = 53,
        G7 = 54,
        H7 = 55,
        A8 = 56,
        B8 = 57,
        C8 = 58,
        D8 = 59,
        E8 = 60,
        F8 = 61,
        G8 = 62,
        H8 = 63,
        NoSquare = 64,
    };

    enum class CastlingRights : std::uint8_t
    {
        None = 0,
        WhiteKingSide = 1 << 0,
        WhiteQueenSide = 1 << 1,
        BlackKingSide = 1 << 2,
        BlackQueenSide = 1 << 3,
        All = WhiteKingSide | WhiteQueenSide | BlackKingSide | BlackQueenSide,
    };

    enum class MoveFlag : std::uint8_t
    {
        Quiet = 0,
        DoublePawnPush = 1,
        KingCastle = 2,
        QueenCastle = 3,
        Capture = 4,
        EnPassant = 5,
        KnightPromotion = 8,
        BishopPromotion = 9,
        RookPromotion = 10,
        QueenPromotion = 11,
        KnightPromotionCapture = 12,
        BishopPromotionCapture = 13,
        RookPromotionCapture = 14,
        QueenPromotionCapture = 15,
    };

    constexpr Color operator~(Color color) noexcept
    {
        return color == Color::White ? Color::Black : Color::White;
    }

    constexpr Bitboard bit(Square square) noexcept
    {
        return static_cast<Bitboard>(1) << static_cast<std::size_t>(square);
    }

    [[nodiscard]] constexpr Move make_move(Square from, Square to, MoveFlag flag = MoveFlag::Quiet) noexcept
    {
        return static_cast<Move>(static_cast<std::uint16_t>(from) | (static_cast<std::uint16_t>(to) << 6) |
                                 (static_cast<std::uint16_t>(flag) << 12));
    }

    [[nodiscard]] constexpr Square move_from(Move move) noexcept
    {
        return static_cast<Square>(move & 0x3Fu);
    }

    [[nodiscard]] constexpr Square move_to(Move move) noexcept
    {
        return static_cast<Square>((move >> 6) & 0x3Fu);
    }

    [[nodiscard]] constexpr MoveFlag move_flag(Move move) noexcept
    {
        return static_cast<MoveFlag>((move >> 12) & 0xFu);
    }

    [[nodiscard]] constexpr bool is_promotion(MoveFlag flag) noexcept
    {
        return static_cast<std::uint8_t>(flag) >= static_cast<std::uint8_t>(MoveFlag::KnightPromotion);
    }

    constexpr std::uint32_t popcount(Bitboard value) noexcept
    {
        return static_cast<std::uint32_t>(std::popcount(value));
    }

    constexpr std::uint32_t lsb_index(Bitboard value) noexcept
    {
        return static_cast<std::uint32_t>(std::countr_zero(value));
    }

    [[nodiscard]] constexpr bool is_white(Piece piece) noexcept
    {
        return piece >= Piece::WhitePawn && piece <= Piece::WhiteKing;
    }

    [[nodiscard]] constexpr bool is_black(Piece piece) noexcept
    {
        return piece >= Piece::BlackPawn && piece <= Piece::BlackKing;
    }

    [[nodiscard]] constexpr Color piece_color(Piece piece) noexcept
    {
        return is_white(piece) ? Color::White : Color::Black;
    }

    [[nodiscard]] constexpr PieceType piece_type(Piece piece) noexcept
    {
        switch (piece)
        {
        case Piece::WhitePawn:
        case Piece::BlackPawn:
            return PieceType::Pawn;
        case Piece::WhiteKnight:
        case Piece::BlackKnight:
            return PieceType::Knight;
        case Piece::WhiteBishop:
        case Piece::BlackBishop:
            return PieceType::Bishop;
        case Piece::WhiteRook:
        case Piece::BlackRook:
            return PieceType::Rook;
        case Piece::WhiteQueen:
        case Piece::BlackQueen:
            return PieceType::Queen;
        case Piece::WhiteKing:
        case Piece::BlackKing:
            return PieceType::King;
        default:
            return PieceType::Count;
        }
    }

    [[nodiscard]] constexpr char piece_to_char(Piece piece) noexcept
    {
        switch (piece)
        {
        case Piece::WhitePawn:
            return 'P';
        case Piece::WhiteKnight:
            return 'N';
        case Piece::WhiteBishop:
            return 'B';
        case Piece::WhiteRook:
            return 'R';
        case Piece::WhiteQueen:
            return 'Q';
        case Piece::WhiteKing:
            return 'K';
        case Piece::BlackPawn:
            return 'p';
        case Piece::BlackKnight:
            return 'n';
        case Piece::BlackBishop:
            return 'b';
        case Piece::BlackRook:
            return 'r';
        case Piece::BlackQueen:
            return 'q';
        case Piece::BlackKing:
            return 'k';
        default:
            return '.';
        }
    }

    [[nodiscard]] constexpr Piece char_to_piece(char c) noexcept
    {
        switch (c)
        {
        case 'P':
            return Piece::WhitePawn;
        case 'N':
            return Piece::WhiteKnight;
        case 'B':
            return Piece::WhiteBishop;
        case 'R':
            return Piece::WhiteRook;
        case 'Q':
            return Piece::WhiteQueen;
        case 'K':
            return Piece::WhiteKing;
        case 'p':
            return Piece::BlackPawn;
        case 'n':
            return Piece::BlackKnight;
        case 'b':
            return Piece::BlackBishop;
        case 'r':
            return Piece::BlackRook;
        case 'q':
            return Piece::BlackQueen;
        case 'k':
            return Piece::BlackKing;
        default:
            return Piece::None;
        }
    }

    class Board
    {
    public:
        static constexpr std::size_t kMaxDirtyPieces = 4;

        struct DirtyPiece
        {
            std::array<Piece, kMaxDirtyPieces> removed_pieces{};
            std::array<Square, kMaxDirtyPieces> removed_squares{};
            std::array<Piece, kMaxDirtyPieces> added_pieces{};
            std::array<Square, kMaxDirtyPieces> added_squares{};
            std::uint8_t removed_count{0};
            std::uint8_t added_count{0};
        };

        Board(std::string_view fen = kStartFen);

        void set_fen(std::string_view fen);
        [[nodiscard]] std::string fen() const;

        [[nodiscard]] Piece piece_on(Square square) const noexcept;
        [[nodiscard]] bool is_empty(Square square) const noexcept;
        [[nodiscard]] Bitboard piece_bb(PieceType type) const noexcept;
        [[nodiscard]] Bitboard occupancy(Color color) const noexcept;
        [[nodiscard]] Bitboard all_occupancy() const noexcept;
        [[nodiscard]] Color side_to_move() const noexcept;
        [[nodiscard]] CastlingRights castling_rights() const noexcept;
        [[nodiscard]] Square en_passant_square() const noexcept;
        [[nodiscard]] std::uint32_t halfmove_clock() const noexcept;
        [[nodiscard]] std::uint32_t fullmove_number() const noexcept;
        [[nodiscard]] Key key() const noexcept;
        [[nodiscard]] Square king_square(Color color) const noexcept;
        [[nodiscard]] Bitboard checkers() const noexcept;
        [[nodiscard]] Bitboard pinned() const noexcept;
        [[nodiscard]] Bitboard pinners() const noexcept;
        [[nodiscard]] const DirtyPiece& last_dirty_piece() const noexcept;

        [[nodiscard]] bool legal(Move move) const;
        bool make_move(Move move);
        bool make_null_move();
        bool undo_move();

    private:
        struct UndoState
        {
            bool null_move{false};
            Move move{0};
            Piece moved{Piece::None};
            Piece captured{Piece::None};
            Square captured_square{Square::NoSquare};
            CastlingRights castling_rights{CastlingRights::None};
            Square en_passant_square{Square::NoSquare};
            std::uint32_t halfmove_clock{0};
            std::uint32_t fullmove_number{1};
            Color side_to_move{Color::White};
            DirtyPiece dirty_piece{};
        };

        void clear();
        void set_piece(Piece piece, Square square);
        void update_state() noexcept;

        std::array<Piece, 64> board_{};
        std::array<Bitboard, static_cast<std::size_t>(PieceType::Count)> pieces_{};
        std::array<Bitboard, 2> occupancy_{};
        Bitboard all_occupancy_{};
        std::array<Square, 2> king_square_{Square::NoSquare, Square::NoSquare};
        Bitboard checkers_{0};
        Bitboard pinned_{0};
        Bitboard pinners_{0};

        Color side_to_move_{Color::White};
        CastlingRights castling_rights_{CastlingRights::All};
        Square en_passant_square_{Square::NoSquare};
        std::uint32_t halfmove_clock_{0};
        std::uint32_t fullmove_number_{1};
        Key key_{0};
        std::array<UndoState, 256> history_{};
        std::size_t history_size_{0};
    };

} // namespace aurora::chess
