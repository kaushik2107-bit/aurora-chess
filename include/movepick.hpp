#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "board.hpp"
#include "evaluation.hpp"
#include "movegen.hpp"

namespace aurora::chess
{

    struct MoveOrdering
    {
        Move tt_move{0};
        std::array<Move, 2> killers{};
        const std::vector<int>* history{nullptr};
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
            Quiets,
            BadNoisy,
            Done,
        };

        [[nodiscard]] bool contains(Move move) const noexcept;
        [[nodiscard]] Move next_from_scored(std::vector<MoveEntry>& moves);
        void score_moves();

        const Board& board_;
        MoveOrdering ordering_;
        MoveList moves_;
        std::vector<MoveEntry> good_noisy_;
        std::vector<MoveEntry> bad_noisy_;
        std::vector<MoveEntry> quiets_;
        Stage stage_{Stage::TtMove};
        std::size_t killer_index_{0};
        bool generated_any_{false};
    };

    [[nodiscard]] bool is_noisy(Move move) noexcept;
    [[nodiscard]] bool is_good_noisy(const Board& board, Move move) noexcept;

} // namespace aurora::chess
