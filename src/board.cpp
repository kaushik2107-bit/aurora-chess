#include "board.hpp"

#include "attacks.hpp"
#include "bitboard.hpp"
#include "helpers.hpp"
#include "zobrist.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace aurora::chess
{
    namespace
    {

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

        constexpr int sign(int value) noexcept
        {
            return (value > 0) - (value < 0);
        }

        constexpr bool is_orthogonal(int file_step, int rank_step) noexcept
        {
            return file_step == 0 || rank_step == 0;
        }

        bool slider_matches_ray(Piece piece, int file_step, int rank_step) noexcept
        {
            const auto type = piece_type(piece);
            if (type == PieceType::Queen)
            {
                return true;
            }
            return is_orthogonal(file_step, rank_step) ? type == PieceType::Rook : type == PieceType::Bishop;
        }

        bool aligned(Square a, Square b, Square c) noexcept
        {
            const int af = file_of(a);
            const int ar = rank_of(a);
            const int bf = file_of(b);
            const int br = rank_of(b);
            const int cf = file_of(c);
            const int cr = rank_of(c);

            const int abf = bf - af;
            const int abr = br - ar;
            const int acf = cf - af;
            const int acr = cr - ar;

            if (abf == 0)
            {
                return acf == 0;
            }
            if (abr == 0)
            {
                return acr == 0;
            }
            return std::abs(abf) == std::abs(abr) && std::abs(acf) == std::abs(acr) && sign(abf) == sign(acf) &&
                   sign(abr) == sign(acr);
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
        king_square_ = {Square::NoSquare, Square::NoSquare};
        checkers_ = 0;
        pinned_ = 0;
        pinners_ = 0;
        side_to_move_ = Color::White;
        castling_rights_ = CastlingRights::All;
        en_passant_square_ = Square::NoSquare;
        halfmove_clock_ = 0;
        fullmove_number_ = 1;
        key_ = 0;
        history_size_ = 0;
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
            if (previous_type == PieceType::King)
            {
                king_square_[static_cast<std::size_t>(piece_color(previous))] = Square::NoSquare;
            }
        }

        board_[idx] = piece;
        if (piece != Piece::None)
        {
            const auto type = piece_type(piece);
            pieces_[static_cast<std::size_t>(type)] |= bit(square);
            occupancy_[static_cast<std::size_t>(piece_color(piece))] |= bit(square);
            all_occupancy_ |= bit(square);
            if (type == PieceType::King)
            {
                king_square_[static_cast<std::size_t>(piece_color(piece))] = square;
            }
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
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) |
                                                               static_cast<int>(CastlingRights::WhiteKingSide));
                break;
            case 'Q':
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) |
                                                               static_cast<int>(CastlingRights::WhiteQueenSide));
                break;
            case 'k':
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) |
                                                               static_cast<int>(CastlingRights::BlackKingSide));
                break;
            case 'q':
                castling_rights_ = static_cast<CastlingRights>(static_cast<int>(castling_rights_) |
                                                               static_cast<int>(CastlingRights::BlackQueenSide));
                break;
            default:
                break;
            }
        }

        if (ep_part == "-")
        {
            en_passant_square_ = Square::NoSquare;
        }
        else if (ep_part.size() == 2 && ep_part[0] >= 'a' && ep_part[0] <= 'h' && ep_part[1] >= '1' &&
                 ep_part[1] <= '8')
        {
            en_passant_square_ = static_cast<Square>((ep_part[1] - '1') * 8 + (ep_part[0] - 'a'));
        }
        else
        {
            throw std::invalid_argument("invalid en passant square");
        }

        halfmove_clock_ = static_cast<std::uint32_t>(std::stoul(halfmove_part));
        fullmove_number_ = static_cast<std::uint32_t>(std::stoul(fullmove_part));
        update_state();
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

    Key Board::key() const noexcept
    {
        return key_;
    }

    Square Board::king_square(Color color) const noexcept
    {
        return king_square_[static_cast<std::size_t>(color)];
    }

    Bitboard Board::checkers() const noexcept
    {
        return checkers_;
    }

    Bitboard Board::pinned() const noexcept
    {
        return pinned_;
    }

    Bitboard Board::pinners() const noexcept
    {
        return pinners_;
    }

    void Board::update_state() noexcept
    {
        checkers_ = 0;
        pinned_ = 0;
        pinners_ = 0;
        key_ = zobrist::hash(*this);

        const Square king = king_square(side_to_move_);
        if (king == Square::NoSquare)
        {
            return;
        }

        checkers_ = attackers_to(*this, king, all_occupancy_, ~side_to_move_);

        constexpr std::array<std::array<int, 2>, 8> directions{{
            {1, 0},
            {-1, 0},
            {0, 1},
            {0, -1},
            {1, 1},
            {1, -1},
            {-1, 1},
            {-1, -1},
        }};

        const int king_file = file_of(king);
        const int king_rank = rank_of(king);
        for (const auto& direction : directions)
        {
            const int file_step = direction[0];
            const int rank_step = direction[1];
            Square blocker = Square::NoSquare;

            for (int file = king_file + file_step, rank = king_rank + rank_step; on_board(file, rank);
                 file += file_step, rank += rank_step)
            {
                const Square square = to_square(file, rank);
                const Bitboard square_bb = bit(square);

                if ((occupancy(side_to_move_) & square_bb) != 0)
                {
                    if (blocker != Square::NoSquare)
                    {
                        break;
                    }
                    blocker = square;
                    continue;
                }

                if ((occupancy(~side_to_move_) & square_bb) != 0)
                {
                    if (blocker != Square::NoSquare && slider_matches_ray(piece_on(square), file_step, rank_step))
                    {
                        pinned_ |= bit(blocker);
                        pinners_ |= square_bb;
                    }
                    break;
                }
            }
        }
    }

    bool Board::legal(Move move) const
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
        const Square king = king_square(us);

        if (flag == MoveFlag::EnPassant)
        {
            const auto captured_square = static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8));
            const Bitboard occupancy_after = (all_occupancy_ & ~bit(from) & ~bit(captured_square)) | bit(to);
            return (attackers_to(*this, king, occupancy_after, them) &
                    ((pieces_[static_cast<std::size_t>(PieceType::Bishop)] |
                      pieces_[static_cast<std::size_t>(PieceType::Rook)] |
                      pieces_[static_cast<std::size_t>(PieceType::Queen)]) &
                     occupancy_[static_cast<std::size_t>(them)])) == 0;
        }

        if (is_castle(flag))
        {
            const int step = to > from ? 1 : -1;
            for (int square = static_cast<int>(from); square != static_cast<int>(to) + step; square += step)
            {
                if (attackers_to(*this, static_cast<Square>(square), all_occupancy_, them) != 0)
                {
                    return false;
                }
            }
            return true;
        }

        if (piece_type(moving) == PieceType::King)
        {
            const Bitboard occupancy_after = (all_occupancy_ & ~bit(from)) & ~bit(to);
            return attackers_to(*this, to, occupancy_after, them) == 0;
        }

        return (pinned_ & bit(from)) == 0 || aligned(king, from, to);
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

        const auto captured_square = flag == MoveFlag::EnPassant
                                         ? static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8))
                                         : to;
        history_[history_size_++] = UndoState{false,
                                              move,
                                              moving,
                                              captured,
                                              captured_square,
                                              castling_rights_,
                                              en_passant_square_,
                                              halfmove_clock_,
                                              fullmove_number_,
                                              side_to_move_};

        if (moving_type == PieceType::King)
        {
            if (us == Color::White)
            {
                castling_rights_ = remove_castling(remove_castling(castling_rights_, CastlingRights::WhiteKingSide),
                                                   CastlingRights::WhiteQueenSide);
            }
            else
            {
                castling_rights_ = remove_castling(remove_castling(castling_rights_, CastlingRights::BlackKingSide),
                                                   CastlingRights::BlackQueenSide);
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

        halfmove_clock_ =
            moving_type == PieceType::Pawn || is_capture(flag) || captured != Piece::None ? 0 : halfmove_clock_ + 1;
        if (us == Color::Black)
        {
            ++fullmove_number_;
        }
        side_to_move_ = them;
        update_state();
        return true;
    }

    bool Board::make_null_move()
    {
        if (checkers_ != 0)
        {
            return false;
        }

        const Color us = side_to_move_;
        history_[history_size_++] = UndoState{true,
                                              0,
                                              Piece::None,
                                              Piece::None,
                                              Square::NoSquare,
                                              castling_rights_,
                                              en_passant_square_,
                                              halfmove_clock_,
                                              fullmove_number_,
                                              side_to_move_};

        en_passant_square_ = Square::NoSquare;
        ++halfmove_clock_;
        if (us == Color::Black)
        {
            ++fullmove_number_;
        }
        side_to_move_ = ~side_to_move_;
        update_state();
        return true;
    }

    bool Board::undo_move()
    {
        if (history_size_ == 0)
        {
            return false;
        }

        const UndoState state = history_[--history_size_];
        if (state.null_move)
        {
            castling_rights_ = state.castling_rights;
            en_passant_square_ = state.en_passant_square;
            halfmove_clock_ = state.halfmove_clock;
            fullmove_number_ = state.fullmove_number;
            side_to_move_ = state.side_to_move;
            update_state();
            return true;
        }

        const Square from = move_from(state.move);
        const Square to = move_to(state.move);
        const MoveFlag flag = move_flag(state.move);

        if (is_castle(flag))
        {
            if (flag == MoveFlag::KingCastle)
            {
                const Square rook_from = state.side_to_move == Color::White ? Square::H1 : Square::H8;
                const Square rook_to = state.side_to_move == Color::White ? Square::F1 : Square::F8;
                set_piece(Piece::None, rook_to);
                set_piece(state.side_to_move == Color::White ? Piece::WhiteRook : Piece::BlackRook, rook_from);
            }
            else
            {
                const Square rook_from = state.side_to_move == Color::White ? Square::A1 : Square::A8;
                const Square rook_to = state.side_to_move == Color::White ? Square::D1 : Square::D8;
                set_piece(Piece::None, rook_to);
                set_piece(state.side_to_move == Color::White ? Piece::WhiteRook : Piece::BlackRook, rook_from);
            }
        }

        set_piece(Piece::None, to);
        set_piece(state.moved, from);

        if (state.captured != Piece::None)
        {
            set_piece(state.captured, state.captured_square);
        }

        castling_rights_ = state.castling_rights;
        en_passant_square_ = state.en_passant_square;
        halfmove_clock_ = state.halfmove_clock;
        fullmove_number_ = state.fullmove_number;
        side_to_move_ = state.side_to_move;
        update_state();
        return true;
    }

} // namespace aurora::chess
