#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "board.hpp"
#include "evaluation.hpp"

namespace aurora::chess
{

    enum class Bound : std::uint8_t
    {
        None,
        Exact,
        Lower,
        Upper,
    };

    struct TranspositionEntry
    {
        Key key{0};
        Move best_move{0};
        Score score{0};
        std::uint16_t depth{0};
        Bound bound{Bound::None};
    };

    class TranspositionTable
    {
    public:
        explicit TranspositionTable(std::size_t entry_count = 1 << 20);

        void clear();
        [[nodiscard]] const TranspositionEntry *probe(Key key) const noexcept;
        void store(Key key, std::size_t depth, Score score, Bound bound, Move best_move) noexcept;

    private:
        std::vector<TranspositionEntry> entries_;
    };

} // namespace aurora::chess
