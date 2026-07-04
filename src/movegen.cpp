#include "movegen.hpp"

#include "attacks.hpp"
#include "bitboard.hpp"
#include "helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace aurora::chess
{
    namespace
    {

        void add_promotions(std::array<MoveEntry, 256> &out, std::size_t &count, Square from, Square to, bool capture)
        {
            out[count++] = MoveEntry{make_move(from, to, capture ? MoveFlag::KnightPromotionCapture : MoveFlag::KnightPromotion), 0};
            out[count++] = MoveEntry{make_move(from, to, capture ? MoveFlag::BishopPromotionCapture : MoveFlag::BishopPromotion), 0};
            out[count++] = MoveEntry{make_move(from, to, capture ? MoveFlag::RookPromotionCapture : MoveFlag::RookPromotion), 0};
            out[count++] = MoveEntry{make_move(from, to, capture ? MoveFlag::QueenPromotionCapture : MoveFlag::QueenPromotion), 0};
        }

    } // namespace

    [[nodiscard]] std::uint32_t MoveGenerator::add_move(std::array<MoveEntry, 256> &out, std::size_t &count, Move move) noexcept
    {
        out[count++] = MoveEntry{move, 0};
        return static_cast<std::uint32_t>(count);
    }

    std::array<MoveEntry, 256> MoveGenerator::generate(const Board &board)
    {
        std::array<MoveEntry, 256> pseudo{};
        std::size_t pseudo_count = 0;

        const auto us = board.side_to_move();

        generate_pawn_moves(board, pseudo, pseudo_count, us);
        generate_knight_moves(board, pseudo, pseudo_count, us);
        generate_bishop_moves(board, pseudo, pseudo_count, us);
        generate_rook_moves(board, pseudo, pseudo_count, us);
        generate_queen_moves(board, pseudo, pseudo_count, us);
        generate_king_moves(board, pseudo, pseudo_count, us);

        std::array<MoveEntry, 256> legal{};
        std::size_t legal_count = 0;
        for (std::size_t i = 0; i < pseudo_count; ++i)
        {
            Board next = board;
            if (next.make_move(pseudo[i].move) && !is_in_check(next, us))
            {
                legal[legal_count++] = pseudo[i];
            }
        }
        return legal;
    }

    void MoveGenerator::generate_pawn_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard pawns = board.piece_bb(PieceType::Pawn) & board.occupancy(us);
        const Bitboard enemies = board.occupancy(~us);
        const auto ep = board.en_passant_square();

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
                    if (rank == promotion_rank)
                    {
                        add_promotions(moves, count, from, one, false);
                    }
                    else
                    {
                        static_cast<void>(add_move(moves, count, make_move(from, one)));
                        const int two_rank = rank + forward * 2;
                        const auto two = to_square(file, two_rank);
                        if (rank == start_rank && board.is_empty(two))
                        {
                            static_cast<void>(add_move(moves, count, make_move(from, two, MoveFlag::DoublePawnPush)));
                        }
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
                if (capture)
                {
                    if (rank == promotion_rank)
                    {
                        add_promotions(moves, count, from, to, true);
                    }
                    else
                    {
                        static_cast<void>(add_move(moves, count, make_move(from, to, MoveFlag::Capture)));
                    }
                }
                else if (to == ep)
                {
                    static_cast<void>(add_move(moves, count, make_move(from, to, MoveFlag::EnPassant)));
                }
            }

            pawns &= pawns - 1;
        }
    }

    void MoveGenerator::generate_knight_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_knights = board.piece_bb(PieceType::Knight) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        const auto enemies = board.occupancy(~us);
        while (our_knights)
        {
            const auto from = static_cast<Square>(lsb_index(our_knights));
            Bitboard targets = knight_attacks(from) & ~friendly;
            while (targets)
            {
                const auto to = static_cast<Square>(lsb_index(targets));
                static_cast<void>(add_move(moves, count, make_move(from, to, (bit(to) & enemies) != 0 ? MoveFlag::Capture : MoveFlag::Quiet)));
                targets &= targets - 1;
            }
            our_knights &= our_knights - 1;
        }
    }

    void MoveGenerator::generate_bishop_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_bishops = board.piece_bb(PieceType::Bishop) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        const auto enemies = board.occupancy(~us);
        while (our_bishops)
        {
            const auto from = static_cast<Square>(lsb_index(our_bishops));
            Bitboard targets = bishop_attacks(from, board.all_occupancy()) & ~friendly;
            while (targets)
            {
                const auto to = static_cast<Square>(lsb_index(targets));
                static_cast<void>(add_move(moves, count, make_move(from, to, (bit(to) & enemies) != 0 ? MoveFlag::Capture : MoveFlag::Quiet)));
                targets &= targets - 1;
            }
            our_bishops &= our_bishops - 1;
        }
    }

    void MoveGenerator::generate_rook_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_rooks = board.piece_bb(PieceType::Rook) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        const auto enemies = board.occupancy(~us);
        while (our_rooks)
        {
            const auto from = static_cast<Square>(lsb_index(our_rooks));
            Bitboard targets = rook_attacks(from, board.all_occupancy()) & ~friendly;
            while (targets)
            {
                const auto to = static_cast<Square>(lsb_index(targets));
                static_cast<void>(add_move(moves, count, make_move(from, to, (bit(to) & enemies) != 0 ? MoveFlag::Capture : MoveFlag::Quiet)));
                targets &= targets - 1;
            }
            our_rooks &= our_rooks - 1;
        }
    }

    void MoveGenerator::generate_queen_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_queens = board.piece_bb(PieceType::Queen) & board.occupancy(us);
        const auto friendly = board.occupancy(us);
        const auto enemies = board.occupancy(~us);
        while (our_queens)
        {
            const auto from = static_cast<Square>(lsb_index(our_queens));
            Bitboard targets = queen_attacks(from, board.all_occupancy()) & ~friendly;
            while (targets)
            {
                const auto to = static_cast<Square>(lsb_index(targets));
                static_cast<void>(add_move(moves, count, make_move(from, to, (bit(to) & enemies) != 0 ? MoveFlag::Capture : MoveFlag::Quiet)));
                targets &= targets - 1;
            }
            our_queens &= our_queens - 1;
        }
    }

    void MoveGenerator::generate_king_moves(const Board &board, std::array<MoveEntry, 256> &moves, std::size_t &count, Color us) const
    {
        Bitboard our_kings = board.piece_bb(PieceType::King) & board.occupancy(us);
        if (our_kings == 0)
        {
            return;
        }

        const auto from = static_cast<Square>(lsb_index(our_kings));
        const auto friendly = board.occupancy(us);
        const auto enemies = board.occupancy(~us);
        Bitboard targets = king_attacks(from) & ~friendly;
        while (targets)
        {
            const auto to = static_cast<Square>(lsb_index(targets));
            static_cast<void>(add_move(moves, count, make_move(from, to, (bit(to) & enemies) != 0 ? MoveFlag::Capture : MoveFlag::Quiet)));
            targets &= targets - 1;
        }

        if (is_in_check(board, us))
        {
            return;
        }

        if (us == Color::White && from == Square::E1)
        {
            if (has_castling(board.castling_rights(), CastlingRights::WhiteKingSide) &&
                board.piece_on(Square::H1) == Piece::WhiteRook &&
                board.is_empty(Square::F1) && board.is_empty(Square::G1) &&
                !is_square_attacked(board, Square::F1, Color::Black) && !is_square_attacked(board, Square::G1, Color::Black))
            {
                static_cast<void>(add_move(moves, count, make_move(Square::E1, Square::G1, MoveFlag::KingCastle)));
            }
            if (has_castling(board.castling_rights(), CastlingRights::WhiteQueenSide) &&
                board.piece_on(Square::A1) == Piece::WhiteRook &&
                board.is_empty(Square::D1) && board.is_empty(Square::C1) && board.is_empty(Square::B1) &&
                !is_square_attacked(board, Square::D1, Color::Black) && !is_square_attacked(board, Square::C1, Color::Black))
            {
                static_cast<void>(add_move(moves, count, make_move(Square::E1, Square::C1, MoveFlag::QueenCastle)));
            }
        }
        else if (us == Color::Black && from == Square::E8)
        {
            if (has_castling(board.castling_rights(), CastlingRights::BlackKingSide) &&
                board.piece_on(Square::H8) == Piece::BlackRook &&
                board.is_empty(Square::F8) && board.is_empty(Square::G8) &&
                !is_square_attacked(board, Square::F8, Color::White) && !is_square_attacked(board, Square::G8, Color::White))
            {
                static_cast<void>(add_move(moves, count, make_move(Square::E8, Square::G8, MoveFlag::KingCastle)));
            }
            if (has_castling(board.castling_rights(), CastlingRights::BlackQueenSide) &&
                board.piece_on(Square::A8) == Piece::BlackRook &&
                board.is_empty(Square::D8) && board.is_empty(Square::C8) && board.is_empty(Square::B8) &&
                !is_square_attacked(board, Square::D8, Color::White) && !is_square_attacked(board, Square::C8, Color::White))
            {
                static_cast<void>(add_move(moves, count, make_move(Square::E8, Square::C8, MoveFlag::QueenCastle)));
            }
        }
    }

} // namespace aurora::chess
