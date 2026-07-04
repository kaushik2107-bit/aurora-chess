#include "ttable.hpp"

#include <algorithm>
#include <limits>

namespace aurora::chess
{

    TranspositionTable::TranspositionTable(std::size_t entry_count) : entries_(std::max<std::size_t>(1, entry_count)) {}

    void TranspositionTable::clear()
    {
        std::fill(entries_.begin(), entries_.end(), TranspositionEntry{});
    }

    const TranspositionEntry *TranspositionTable::probe(Key key) const noexcept
    {
        const auto &entry = entries_[static_cast<std::size_t>(key % entries_.size())];
        return entry.key == key && entry.bound != Bound::None ? &entry : nullptr;
    }

    void TranspositionTable::store(Key key, std::size_t depth, Score score, Bound bound, Move best_move) noexcept
    {
        auto &entry = entries_[static_cast<std::size_t>(key % entries_.size())];
        if (entry.bound != Bound::None && entry.key == key && entry.depth > depth && best_move == 0)
        {
            return;
        }

        entry = TranspositionEntry{
            key,
            best_move,
            score,
            static_cast<std::uint16_t>(std::min<std::size_t>(depth, std::numeric_limits<std::uint16_t>::max())),
            bound,
        };
    }

} // namespace aurora::chess
