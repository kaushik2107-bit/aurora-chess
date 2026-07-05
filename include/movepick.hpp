#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "board.hpp"
#include "evaluation.hpp"
#include "movegen.hpp"

namespace aurora::chess
{

    inline constexpr std::size_t kPieceSquareHistoryBuckets = 12 * 64;
    inline constexpr std::size_t kNoPieceSquareHistory = kPieceSquareHistoryBuckets;

    [[nodiscard]] constexpr std::size_t piece_square_history_index(Piece piece, Square square) noexcept
    {
        if (piece == Piece::None || square == Square::NoSquare)
        {
            return kNoPieceSquareHistory;
        }

        return (static_cast<std::size_t>(piece) - 1) * 64 + static_cast<std::size_t>(square);
    }

    struct MoveOrdering
    {
        Move tt_move{0};
        Move counter_move{0};
        std::array<Move, 2> killers{};
        const std::vector<int>* history{nullptr};
        const std::vector<int>* continuation_history{nullptr};
        std::size_t previous_piece_square{kNoPieceSquareHistory};
    };

    class MovePicker
    {
    public:
        MovePicker(const Board& board, MoveOrdering ordering = {});

        [[nodiscard]] Move next();
        [[nodiscard]] bool generated_any() const noexcept;

    private:
        enum class Stage
        {
            TtMove,
            GoodNoisy,
            Killers,
            CounterMove,
            Quiets,
            BadNoisy,
            Done,
        };

        [[nodiscard]] bool contains(Move move) const noexcept;
        [[nodiscard]] Move next_from_scored(MoveList& moves);
        void score_moves();

        const Board& board_;
        MoveOrdering ordering_;
        MoveList moves_;
        MoveList good_noisy_;
        MoveList bad_noisy_;
        MoveList quiets_;
        Stage stage_{Stage::TtMove};
        std::size_t killer_index_{0};
        bool generated_any_{false};
    };

    [[nodiscard]] bool is_noisy(Move move) noexcept;
    [[nodiscard]] bool is_good_noisy(const Board& board, Move move) noexcept;

} // namespace aurora::chess
