#include "movegen.hpp"

#include "attacks.hpp"
#include "bitboard.hpp"
#include "helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>

namespace aurora::chess
{
    namespace
    {

        void add_move(std::array<MoveEntry, 256> &out, std::size_t &count, Move move) noexcept
        {
            out[count++] = MoveEntry{move, 0};
        }

        void add_promotions(std::array<MoveEntry, 256> &out, std::size_t &count, Square from, Square to, bool capture)
        {
            add_move(out, count, make_move(from, to, capture ? MoveFlag::KnightPromotionCapture : MoveFlag::KnightPromotion));
            add_move(out, count, make_move(from, to, capture ? MoveFlag::BishopPromotionCapture : MoveFlag::BishopPromotion));
            add_move(out, count, make_move(from, to, capture ? MoveFlag::RookPromotionCapture : MoveFlag::RookPromotion));
            add_move(out, count, make_move(from, to, capture ? MoveFlag::QueenPromotionCapture : MoveFlag::QueenPromotion));
        }

        constexpr int sign(int value) noexcept
        {
            return (value > 0) - (value < 0);
        }

        Bitboard squares_between(Square from, Square to) noexcept
        {
            const int from_file = file_of(from);
            const int from_rank = rank_of(from);
            const int to_file = file_of(to);
            const int to_rank = rank_of(to);
            const int file_delta = to_file - from_file;
            const int rank_delta = to_rank - from_rank;

            if (file_delta != 0 && rank_delta != 0 && std::abs(file_delta) != std::abs(rank_delta))
            {
                return 0;
            }

            const int file_step = sign(file_delta);
            const int rank_step = sign(rank_delta);
            Bitboard between = 0;
            for (int file = from_file + file_step, rank = from_rank + rank_step;
                 file != to_file || rank != to_rank;
                 file += file_step, rank += rank_step)
            {
                between |= bit(to_square(file, rank));
            }
            return between;
        }

        bool needs_full_legality_check(const Board &board, Move move) noexcept
        {
            const Square from = move_from(move);
            const MoveFlag flag = move_flag(move);
            return is_castle(flag) || flag == MoveFlag::EnPassant ||
                   piece_type(board.piece_on(from)) == PieceType::King ||
                   (board.pinned() & bit(from)) != 0;
        }

        void add_if_legal(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Move move)
        {
            if (!needs_full_legality_check(board, move) || board.legal(move))
            {
                add_move(moves, count, move);
            }
        }

        void generate_pawn_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Bitboard target)
        {
            const Color us = board.side_to_move();
            Bitboard pawns = board.piece_bb(PieceType::Pawn) & board.occupancy(us);
            const Bitboard enemies = board.occupancy(~us);
            const Square ep = board.en_passant_square();

            while (pawns)
            {
                const auto from = static_cast<Square>(lsb_index(pawns));
                const int file = file_of(from);
                const int rank = rank_of(from);
                const int forward = us == Color::White ? 1 : -1;
                const int start_rank = us == Color::White ? 1 : 6;
                const int promotion_rank = us == Color::White ? 6 : 1;

                const int one_rank = rank + forward;
                if (on_board(file, one_rank))
                {
                    const auto one = to_square(file, one_rank);
                    if (board.is_empty(one))
                    {
                        if ((bit(one) & target) != 0)
                        {
                            if (rank == promotion_rank)
                            {
                                if ((board.pinned() & bit(from)) == 0 || board.legal(make_move(from, one, MoveFlag::QueenPromotion)))
                                {
                                    add_promotions(moves, count, from, one, false);
                                }
                            }
                            else
                            {
                                add_if_legal(board, moves, count, make_move(from, one));
                            }
                        }

                        const int two_rank = rank + forward * 2;
                        const auto two = to_square(file, two_rank);
                        if (rank == start_rank && board.is_empty(two) && (bit(two) & target) != 0)
                        {
                            add_if_legal(board, moves, count, make_move(from, two, MoveFlag::DoublePawnPush));
                        }
                    }
                }

                for (const int df : {-1, 1})
                {
                    const int target_file = file + df;
                    const int target_rank = rank + forward;
                    if (!on_board(target_file, target_rank))
                    {
                        continue;
                    }

                    const auto to = to_square(target_file, target_rank);
                    const bool capture = (bit(to) & enemies) != 0;
                    if (capture && (bit(to) & target) != 0)
                    {
                        if (rank == promotion_rank)
                        {
                            if ((board.pinned() & bit(from)) == 0 || board.legal(make_move(from, to, MoveFlag::QueenPromotionCapture)))
                            {
                                add_promotions(moves, count, from, to, true);
                            }
                        }
                        else
                        {
                            add_if_legal(board, moves, count, make_move(from, to, MoveFlag::Capture));
                        }
                    }
                    else if (to == ep)
                    {
                        const auto captured_square = static_cast<Square>(static_cast<int>(to) + (us == Color::White ? -8 : 8));
                        if ((board.checkers() == 0 || ((bit(to) | bit(captured_square)) & target) != 0) &&
                            board.legal(make_move(from, to, MoveFlag::EnPassant)))
                        {
                            add_move(moves, count, make_move(from, to, MoveFlag::EnPassant));
                        }
                    }
                }

                pawns &= pawns - 1;
            }
        }

        template <PieceType Type>
        void generate_piece_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Bitboard target)
        {
            Bitboard pieces = board.piece_bb(Type) & board.occupancy(board.side_to_move());
            const Bitboard enemies = board.occupancy(~board.side_to_move());

            while (pieces)
            {
                const auto from = static_cast<Square>(lsb_index(pieces));
                Bitboard attacks = 0;
                if constexpr (Type == PieceType::Knight)
                {
                    if ((board.pinned() & bit(from)) != 0)
                    {
                        pieces &= pieces - 1;
                        continue;
                    }
                    attacks = knight_attacks(from);
                }
                else if constexpr (Type == PieceType::Bishop)
                {
                    attacks = bishop_attacks(from, board.all_occupancy());
                }
                else if constexpr (Type == PieceType::Rook)
                {
                    attacks = rook_attacks(from, board.all_occupancy());
                }
                else if constexpr (Type == PieceType::Queen)
                {
                    attacks = queen_attacks(from, board.all_occupancy());
                }

                Bitboard destinations = attacks & target;
                while (destinations)
                {
                    const auto to = static_cast<Square>(lsb_index(destinations));
                    add_if_legal(board, moves, count,
                                 make_move(from, to, (bit(to) & enemies) != 0 ? MoveFlag::Capture : MoveFlag::Quiet));
                    destinations &= destinations - 1;
                }
                pieces &= pieces - 1;
            }
        }

        void generate_king_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count)
        {
            const Color us = board.side_to_move();
            const Square from = board.king_square(us);
            if (from == Square::NoSquare)
            {
                return;
            }

            const Bitboard friendly = board.occupancy(us);
            const Bitboard enemies = board.occupancy(~us);
            Bitboard destinations = king_attacks(from) & ~friendly;
            while (destinations)
            {
                const auto to = static_cast<Square>(lsb_index(destinations));
                const Move move = make_move(from, to, (bit(to) & enemies) != 0 ? MoveFlag::Capture : MoveFlag::Quiet);
                if (board.legal(move))
                {
                    add_move(moves, count, move);
                }
                destinations &= destinations - 1;
            }

            if (board.checkers() != 0)
            {
                return;
            }

            if (us == Color::White && from == Square::E1)
            {
                if (has_castling(board.castling_rights(), CastlingRights::WhiteKingSide) &&
                    board.piece_on(Square::H1) == Piece::WhiteRook &&
                    board.is_empty(Square::F1) && board.is_empty(Square::G1) &&
                    board.legal(make_move(Square::E1, Square::G1, MoveFlag::KingCastle)))
                {
                    add_move(moves, count, make_move(Square::E1, Square::G1, MoveFlag::KingCastle));
                }
                if (has_castling(board.castling_rights(), CastlingRights::WhiteQueenSide) &&
                    board.piece_on(Square::A1) == Piece::WhiteRook &&
                    board.is_empty(Square::D1) && board.is_empty(Square::C1) && board.is_empty(Square::B1) &&
                    board.legal(make_move(Square::E1, Square::C1, MoveFlag::QueenCastle)))
                {
                    add_move(moves, count, make_move(Square::E1, Square::C1, MoveFlag::QueenCastle));
                }
            }
            else if (us == Color::Black && from == Square::E8)
            {
                if (has_castling(board.castling_rights(), CastlingRights::BlackKingSide) &&
                    board.piece_on(Square::H8) == Piece::BlackRook &&
                    board.is_empty(Square::F8) && board.is_empty(Square::G8) &&
                    board.legal(make_move(Square::E8, Square::G8, MoveFlag::KingCastle)))
                {
                    add_move(moves, count, make_move(Square::E8, Square::G8, MoveFlag::KingCastle));
                }
                if (has_castling(board.castling_rights(), CastlingRights::BlackQueenSide) &&
                    board.piece_on(Square::A8) == Piece::BlackRook &&
                    board.is_empty(Square::D8) && board.is_empty(Square::C8) && board.is_empty(Square::B8) &&
                    board.legal(make_move(Square::E8, Square::C8, MoveFlag::QueenCastle)))
                {
                    add_move(moves, count, make_move(Square::E8, Square::C8, MoveFlag::QueenCastle));
                }
            }
        }

    } // namespace

    std::array<MoveEntry, 256> MoveGenerator::generate(const Board &board)
    {
        std::array<MoveEntry, 256> moves{};
        std::size_t count = 0;

        const Bitboard friendly = board.occupancy(board.side_to_move());
        Bitboard target = ~friendly;

        const std::uint32_t check_count = popcount(board.checkers());
        if (check_count == 1)
        {
            const Square checker = static_cast<Square>(lsb_index(board.checkers()));
            target = bit(checker) | squares_between(board.king_square(board.side_to_move()), checker);
        }

        generate_king_moves(board, moves, count);

        if (check_count > 1)
        {
            return moves;
        }

        generate_pawn_moves(board, moves, count, target);
        generate_piece_moves<PieceType::Knight>(board, moves, count, target);
        generate_piece_moves<PieceType::Bishop>(board, moves, count, target);
        generate_piece_moves<PieceType::Rook>(board, moves, count, target);
        generate_piece_moves<PieceType::Queen>(board, moves, count, target);
        return moves;
    }

} // namespace aurora::chess
