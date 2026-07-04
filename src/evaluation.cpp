#include "evaluation.hpp"

#include "bitboard.hpp"

#include <array>
#include <cstddef>

namespace aurora::chess
{
    namespace
    {

        constexpr Score kTempo = 10;
        constexpr Score kBishopPair = 30;

        constexpr std::array<Score, static_cast<std::size_t>(PieceType::Count)> kPieceValues{
            100, 320, 330, 500, 900, 0,
        };

        // clang-format off
        constexpr std::array<Score, 64> kPawnPsqt = {
              0,   0,   0,   0,   0,   0,   0,   0,
             50,  50,  50,  50,  50,  50,  50,  50,
             10,  10,  20,  30,  30,  20,  10,  10,
              5,   5,  10,  25,  25,  10,   5,   5,
              0,   0,   0,  20,  20,   0,   0,   0,
              5,  -5, -10,   0,   0, -10,  -5,   5,
              5,  10,  10, -20, -20,  10,  10,   5,
              0,   0,   0,   0,   0,   0,   0,   0,
        };

        constexpr std::array<Score, 64> kKnightPsqt = {
            -50, -40, -30, -30, -30, -30, -40, -50,
            -40, -20,   0,   5,   5,   0, -20, -40,
            -30,   5,  10,  15,  15,  10,   5, -30,
            -30,   0,  15,  20,  20,  15,   0, -30,
            -30,   5,  15,  20,  20,  15,   5, -30,
            -30,   0,  10,  15,  15,  10,   0, -30,
            -40, -20,   0,   0,   0,   0, -20, -40,
            -50, -40, -30, -30, -30, -30, -40, -50,
        };

        constexpr std::array<Score, 64> kBishopPsqt = {
            -20, -10, -10, -10, -10, -10, -10, -20,
            -10,   5,   0,   0,   0,   0,   5, -10,
            -10,  10,  10,  10,  10,  10,  10, -10,
            -10,   0,  10,  10,  10,  10,   0, -10,
            -10,   5,   5,  10,  10,   5,   5, -10,
            -10,   0,   5,  10,  10,   5,   0, -10,
            -10,   0,   0,   0,   0,   0,   0, -10,
            -20, -10, -10, -10, -10, -10, -10, -20,
        };

        constexpr std::array<Score, 64> kRookPsqt = {
              0,   0,   0,   5,   5,   0,   0,   0,
             -5,   0,   0,   0,   0,   0,   0,  -5,
             -5,   0,   0,   0,   0,   0,   0,  -5,
             -5,   0,   0,   0,   0,   0,   0,  -5,
             -5,   0,   0,   0,   0,   0,   0,  -5,
             -5,   0,   0,   0,   0,   0,   0,  -5,
              5,  10,  10,  10,  10,  10,  10,   5,
              0,   0,   0,   0,   0,   0,   0,   0,
        };

        constexpr std::array<Score, 64> kQueenPsqt = {
            -20, -10, -10,  -5,  -5, -10, -10, -20,
            -10,   0,   5,   0,   0,   0,   0, -10,
            -10,   5,   5,   5,   5,   5,   0, -10,
              0,   0,   5,   5,   5,   5,   0,  -5,
             -5,   0,   5,   5,   5,   5,   0,  -5,
            -10,   0,   5,   5,   5,   5,   0, -10,
            -10,   0,   0,   0,   0,   0,   0, -10,
            -20, -10, -10,  -5,  -5, -10, -10, -20,
        };

        constexpr std::array<Score, 64> kKingPsqt = {
             20,  30,  10,   0,   0,  10,  30,  20,
             20,  20,   0,   0,   0,   0,  20,  20,
            -10, -20, -20, -20, -20, -20, -20, -10,
            -20, -30, -30, -40, -40, -30, -30, -20,
            -30, -40, -40, -50, -50, -40, -40, -30,
            -30, -40, -40, -50, -50, -40, -40, -30,
            -30, -40, -40, -50, -50, -40, -40, -30,
            -30, -40, -40, -50, -50, -40, -40, -30,
        };
        // clang-format on

        constexpr std::array<std::array<Score, 64>, static_cast<std::size_t>(PieceType::Count)> kPsqt{
            kPawnPsqt, kKnightPsqt, kBishopPsqt, kRookPsqt, kQueenPsqt, kKingPsqt,
        };

        [[nodiscard]] constexpr Square relative_square(Color color, Square square) noexcept
        {
            return color == Color::White ? square : static_cast<Square>(static_cast<int>(square) ^ 56);
        }

        [[nodiscard]] Score evaluate_piece_set(const Board &board, Color color) noexcept
        {
            Score score = 0;
            const Bitboard own = board.occupancy(color);
            for (std::size_t type_index = 0; type_index < static_cast<std::size_t>(PieceType::Count); ++type_index)
            {
                const auto type = static_cast<PieceType>(type_index);
                Bitboard pieces = board.piece_bb(type) & own;
                while (pieces != 0)
                {
                    const auto square = static_cast<Square>(lsb_index(pieces));
                    const auto psqt_square = relative_square(color, square);
                    score += kPieceValues[type_index] + kPsqt[type_index][static_cast<std::size_t>(psqt_square)];
                    pieces &= pieces - 1;
                }
            }

            if (popcount(board.piece_bb(PieceType::Bishop) & own) >= 2)
            {
                score += kBishopPair;
            }
            return score;
        }

    } // namespace

    Score PsqtEvaluator::evaluate(const Board &board) const noexcept
    {
        const Score white = evaluate_piece_set(board, Color::White);
        const Score black = evaluate_piece_set(board, Color::Black);
        const Score score = white - black;
        return (board.side_to_move() == Color::White ? score : -score) + kTempo;
    }

    const Evaluator &default_evaluator() noexcept
    {
        static const PsqtEvaluator evaluator;
        return evaluator;
    }

} // namespace aurora::chess
