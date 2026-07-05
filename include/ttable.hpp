#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
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
        Score static_eval{0};
        std::uint16_t depth{0};
        Bound bound{Bound::None};
        bool has_static_eval{false};
    };

    class TranspositionTable
    {
    public:
        explicit TranspositionTable(std::size_t entry_count = 1 << 20);

        void clear();
        void resize(std::size_t entry_count);
        [[nodiscard]] std::size_t entry_count() const;
        [[nodiscard]] std::optional<TranspositionEntry> probe(Key key) const;
        void store(Key key, std::size_t depth, Score score, Bound bound, Move best_move,
                   std::optional<Score> static_eval = std::nullopt);

    private:
        mutable std::shared_mutex mutex_;
        std::vector<TranspositionEntry> entries_;
    };

} // namespace aurora::chess
