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

        en_passant_square_ = ep_part == "-" ? Square::NoSquare : static_cast<Square>(std::stoi(ep_part.substr(1)) + (ep_part[0] - 'a'));

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
        stream << ' ' << (en_passant_square_ == Square::NoSquare ? "-" : "e3") << ' ';
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

} // namespace aurora::chess
