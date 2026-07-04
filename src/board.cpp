#include "board.hpp"

#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace aurora::chess
{
    namespace
    {

        constexpr std::array<Square, 64> kSquares = {
            Square::A1,
            Square::B1,
            Square::C1,
            Square::D1,
            Square::E1,
            Square::F1,
            Square::G1,
            Square::H1,
            Square::A2,
            Square::B2,
            Square::C2,
            Square::D2,
            Square::E2,
            Square::F2,
            Square::G2,
            Square::H2,
            Square::A3,
            Square::B3,
            Square::C3,
            Square::D3,
            Square::E3,
            Square::F3,
            Square::G3,
            Square::H3,
            Square::A4,
            Square::B4,
            Square::C4,
            Square::D4,
            Square::E4,
            Square::F4,
            Square::G4,
            Square::H4,
            Square::A5,
            Square::B5,
            Square::C5,
            Square::D5,
            Square::E5,
            Square::F5,
            Square::G5,
            Square::H5,
            Square::A6,
            Square::B6,
            Square::C6,
            Square::D6,
            Square::E6,
            Square::F6,
            Square::G6,
            Square::H6,
            Square::A7,
            Square::B7,
            Square::C7,
            Square::D7,
            Square::E7,
            Square::F7,
            Square::G7,
            Square::H7,
            Square::A8,
            Square::B8,
            Square::C8,
            Square::D8,
            Square::E8,
            Square::F8,
            Square::G8,
            Square::H8,
        };

        constexpr std::size_t square_index(Square square) noexcept
        {
            return static_cast<std::size_t>(square);
        }

        constexpr Square make_square(std::size_t index) noexcept
        {
            return static_cast<Square>(index);
        }

        constexpr CastlingRights remove_castling(CastlingRights rights, CastlingRights flag) noexcept
        {
            return static_cast<CastlingRights>(static_cast<int>(rights) & ~static_cast<int>(flag));
        }

        constexpr Piece make_piece(Color color, PieceType type) noexcept
        {
            if (color == Color::White)
            {
                switch (type)
                {
                case PieceType::Pawn:
                    return Piece::WhitePawn;
                case PieceType::Knight:
                    return Piece::WhiteKnight;
                case PieceType::Bishop:
                    return Piece::WhiteBishop;
                case PieceType::Rook:
                    return Piece::WhiteRook;
                case PieceType::Queen:
                    return Piece::WhiteQueen;
                case PieceType::King:
                    return Piece::WhiteKing;
                default:
                    return Piece::None;
                }
            }

            switch (type)
            {
            case PieceType::Pawn:
                return Piece::BlackPawn;
            case PieceType::Knight:
                return Piece::BlackKnight;
            case PieceType::Bishop:
                return Piece::BlackBishop;
            case PieceType::Rook:
                return Piece::BlackRook;
            case PieceType::Queen:
                return Piece::BlackQueen;
            case PieceType::King:
                return Piece::BlackKing;
            default:
                return Piece::None;
            }
        }

        constexpr PieceType promotion_type(MoveFlag flag) noexcept
        {
            switch (flag)
            {
            case MoveFlag::KnightPromotion:
            case MoveFlag::KnightPromotionCapture:
                return PieceType::Knight;
            case MoveFlag::BishopPromotion:
            case MoveFlag::BishopPromotionCapture:
                return PieceType::Bishop;
            case MoveFlag::RookPromotion:
            case MoveFlag::RookPromotionCapture:
                return PieceType::Rook;
            case MoveFlag::QueenPromotion:
            case MoveFlag::QueenPromotionCapture:
                return PieceType::Queen;
            default:
                return PieceType::Count;
            }
        }

        constexpr bool is_capture_flag(MoveFlag flag) noexcept
        {
            return flag == MoveFlag::Capture || flag == MoveFlag::EnPassant ||
                   flag == MoveFlag::KnightPromotionCapture || flag == MoveFlag::BishopPromotionCapture ||
                   flag == MoveFlag::RookPromotionCapture || flag == MoveFlag::QueenPromotionCapture;
        }

        std::string square_name(Square square)
        {
            if (square == Square::NoSquare)
            {
                return "-";
            }
            const auto index = square_index(square);
            std::string name;
            name.push_back(static_cast<char>('a' + (index % 8)));
            name.push_back(static_cast<char>('1' + (index / 8)));
            return name;
        }

    } // namespace

    Board::Board(std::string_view fen)
    {
        set_fen(fen);
    }

    void Board::clear()
    {
        board_.fill(Piece::None);
        pieces_.fill(0);
        occupancy_.fill(0);
        all_occupancy_ = 0;
        side_to_move_ = Color::White;
        castling_rights_ = CastlingRights::All;
        en_passant_square_ = Square::NoSquare;
        halfmove_clock_ = 0;
        fullmove_number_ = 1;
    }

    void Board::set_piece(Piece piece, Square square)
    {
        const auto idx = square_index(square);
        const auto previous = board_[idx];
        if (previous != Piece::None)
        {
            const auto previous_type = piece_type(previous);
            pieces_[static_cast<std::size_t>(previous_type)] &= ~bit(square);
            occupancy_[static_cast<std::size_t>(piece_color(previous))] &= ~bit(square);
            all_occupancy_ &= ~bit(square);
        }

        board_[idx] = piece;
        if (piece != Piece::None)
        {
            const auto type = piece_type(piece);
            pieces_[static_cast<std::size_t>(type)] |= bit(square);
            occupancy_[static_cast<std::size_t>(piece_color(piece))] |= bit(square);
            all_occupancy_ |= bit(square);
        }
    }

    void Board::set_fen(std::string_view fen)
    {
        clear();

        std::istringstream stream{std::string{fen}};
        std::string board_part;
        std::string side_part;
        std::string castling_part;
        std::string ep_part;
        std::string halfmove_part;
        std::string fullmove_part;

        if (!(stream >> board_part >> side_part >> castling_part >> ep_part >> halfmove_part >> fullmove_part))
        {
            throw std::invalid_argument("invalid FEN");
        }

        std::size_t file = 0;
        std::size_t rank = 7;
        for (char ch : board_part)
        {
            if (ch == '/')
            {
                --rank;
                file = 0;
            }
            else if (std::isdigit(static_cast<unsigned char>(ch)))
            {
                file += static_cast<std::size_t>(ch - '0');
            }
            else
            {
                const auto square = make_square(rank * 8 + file);
                set_piece(char_to_piece(ch), square);
                ++file;
            }
        }

        side_to_move_ = side_part == "w" ? Color::White : Color::Black;

        castling_rights_ = CastlingRights::None;
        for (char ch : castling_part)
        {
            switch (ch)
            {
            case 'K':
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) | static_cast<int>(CastlingRights::WhiteKingSide));
                break;
            case 'Q':
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) | static_cast<int>(CastlingRights::WhiteQueenSide));
                break;
            case 'k':
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) | static_cast<int>(CastlingRights::BlackKingSide));
                break;
            case 'q':
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) | static_cast<int>(CastlingRights::BlackQueenSide));
                break;
            default:
                break;
            }
        }

        if (ep_part == "-")
        {
            en_passant_square_ = Square::NoSquare;
        }
        else if (ep_part.size() == 2 && ep_part[0] >= 'a' && ep_part[0] <= 'h' && ep_part[1] >= '1' && ep_part[1] <= '8')
        {
            en_passant_square_ = static_cast<Square>((ep_part[1] - '1') * 8 + (ep_part[0] - 'a'));
        }
        else
        {
            throw std::invalid_argument("invalid en passant square");
        }

        halfmove_clock_ = static_cast<std::uint32_t>(std::stoul(halfmove_part));
        fullmove_number_ = static_cast<std::uint32_t>(std::stoul(fullmove_part));
    }

    std::string Board::fen() const
    {
        std::ostringstream stream;
        for (std::size_t rank = 7; rank != static_cast<std::size_t>(-1); --rank)
        {
            std::size_t empty = 0;
            for (std::size_t file = 0; file < 8; ++file)
            {
                const auto square = static_cast<Square>(rank * 8 + file);
                const auto piece = board_[static_cast<std::size_t>(square)];
                if (piece == Piece::None)
                {
                    ++empty;
                }
                else
                {
                    if (empty != 0)
                    {
                        stream << empty;
                        empty = 0;
                    }
                    stream << piece_to_char(piece);
                }
            }
            if (empty != 0)
            {
                stream << empty;
            }
            if (rank != 0)
            {
                stream << '/';
            }
        }
        stream << ' ' << (side_to_move_ == Color::White ? 'w' : 'b') << ' ';
        if (castling_rights_ == CastlingRights::None)
        {
            stream << '-';
        }
        else
        {
            if (static_cast<int>(castling_rights_) & static_cast<int>(CastlingRights::WhiteKingSide))
            {
                stream << 'K';
            }
            if (static_cast<int>(castling_rights_) & static_cast<int>(CastlingRights::WhiteQueenSide))
            {
                stream << 'Q';
            }
            if (static_cast<int>(castling_rights_) & static_cast<int>(CastlingRights::BlackKingSide))
            {
                stream << 'k';
            }
            if (static_cast<int>(castling_rights_) & static_cast<int>(CastlingRights::BlackQueenSide))
            {
                stream << 'q';
            }
        }
        stream << ' ' << square_name(en_passant_square_) << ' ';
        stream << halfmove_clock_ << ' ' << fullmove_number_;
        return stream.str();
    }

    Piece Board::piece_on(Square square) const noexcept
    {
        return board_[static_cast<std::size_t>(square)];
    }

    bool Board::is_empty(Square square) const noexcept
    {
        return board_[static_cast<std::size_t>(square)] == Piece::None;
    }

    Bitboard Board::piece_bb(PieceType type) const noexcept
    {
        return pieces_[static_cast<std::size_t>(type)];
    }

    Bitboard Board::occupancy(Color color) const noexcept
    {
        return occupancy_[static_cast<std::size_t>(color)];
    }

    Bitboard Board::all_occupancy() const noexcept
    {
        return all_occupancy_;
    }

    Color Board::side_to_move() const noexcept
    {
        return side_to_move_;
    }

    CastlingRights Board::castling_rights() const noexcept
    {
        return castling_rights_;
    }

    Square Board::en_passant_square() const noexcept
    {
        return en_passant_square_;
    }

    std::uint32_t Board::halfmove_clock() const noexcept
    {
        return halfmove_clock_;
    }

    std::uint32_t Board::fullmove_number() const noexcept
    {
        return fullmove_number_;
    }

    bool Board::make_move(Move move)
    {
        const auto from = move_from(move);
        const auto to = move_to(move);
        const auto flag = move_flag(move);
        const auto moving = piece_on(from);
        if (moving == Piece::None || piece_color(moving) != side_to_move_)
        {
            return false;
        }

        const auto us = side_to_move_;
        const auto them = ~us;
        const auto moving_type = piece_type(moving);
        const auto captured = flag == MoveFlag::EnPassant
                                  ? piece_on(static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8)))
                                  : piece_on(to);

        if (captured != Piece::None && piece_color(captured) == us)
        {
            return false;
        }

        if (moving_type == PieceType::King)
        {
            if (us == Color::White)
            {
                castling_rights_ = remove_castling(remove_castling(castling_rights_, CastlingRights::WhiteKingSide), CastlingRights::WhiteQueenSide);
            }
            else
            {
                castling_rights_ = remove_castling(remove_castling(castling_rights_, CastlingRights::BlackKingSide), CastlingRights::BlackQueenSide);
            }
        }
        else if (moving_type == PieceType::Rook)
        {
            if (from == Square::A1)
            {
                castling_rights_ = remove_castling(castling_rights_, CastlingRights::WhiteQueenSide);
            }
            else if (from == Square::H1)
            {
                castling_rights_ = remove_castling(castling_rights_, CastlingRights::WhiteKingSide);
            }
            else if (from == Square::A8)
            {
                castling_rights_ = remove_castling(castling_rights_, CastlingRights::BlackQueenSide);
            }
            else if (from == Square::H8)
            {
                castling_rights_ = remove_castling(castling_rights_, CastlingRights::BlackKingSide);
            }
        }

        if (to == Square::A1)
        {
            castling_rights_ = remove_castling(castling_rights_, CastlingRights::WhiteQueenSide);
        }
        else if (to == Square::H1)
        {
            castling_rights_ = remove_castling(castling_rights_, CastlingRights::WhiteKingSide);
        }
        else if (to == Square::A8)
        {
            castling_rights_ = remove_castling(castling_rights_, CastlingRights::BlackQueenSide);
        }
        else if (to == Square::H8)
        {
            castling_rights_ = remove_castling(castling_rights_, CastlingRights::BlackKingSide);
        }

        set_piece(Piece::None, from);

        if (flag == MoveFlag::EnPassant)
        {
            const auto captured_square = static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8));
            set_piece(Piece::None, captured_square);
        }

        Piece placed = moving;
        if (is_promotion(flag))
        {
            placed = make_piece(us, promotion_type(flag));
        }
        set_piece(placed, to);

        if (flag == MoveFlag::KingCastle)
        {
            if (us == Color::White)
            {
                set_piece(Piece::None, Square::H1);
                set_piece(Piece::WhiteRook, Square::F1);
            }
            else
            {
                set_piece(Piece::None, Square::H8);
                set_piece(Piece::BlackRook, Square::F8);
            }
        }
        else if (flag == MoveFlag::QueenCastle)
        {
            if (us == Color::White)
            {
                set_piece(Piece::None, Square::A1);
                set_piece(Piece::WhiteRook, Square::D1);
            }
            else
            {
                set_piece(Piece::None, Square::A8);
                set_piece(Piece::BlackRook, Square::D8);
            }
        }

        en_passant_square_ = Square::NoSquare;
        if (flag == MoveFlag::DoublePawnPush)
        {
            en_passant_square_ = static_cast<Square>((static_cast<int>(from) + static_cast<int>(to)) / 2);
        }

        halfmove_clock_ = moving_type == PieceType::Pawn || is_capture_flag(flag) || captured != Piece::None ? 0 : halfmove_clock_ + 1;
        if (us == Color::Black)
        {
            ++fullmove_number_;
        }
        side_to_move_ = them;
        return true;
    }

} // namespace aurora::chess
