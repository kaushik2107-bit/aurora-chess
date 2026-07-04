#include "ttable.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <shared_mutex>

namespace aurora::chess
{

    TranspositionTable::TranspositionTable(std::size_t entry_count) : entries_(std::max<std::size_t>(1, entry_count)) {}

    void TranspositionTable::clear()
    {
        const std::unique_lock lock{mutex_};
        std::fill(entries_.begin(), entries_.end(), TranspositionEntry{});
    }

    void TranspositionTable::resize(std::size_t entry_count)
    {
        const std::unique_lock lock{mutex_};
        entries_.assign(std::max<std::size_t>(1, entry_count), TranspositionEntry{});
    }

    std::size_t TranspositionTable::entry_count() const
    {
        const std::shared_lock lock{mutex_};
        return entries_.size();
    }

    std::optional<TranspositionEntry> TranspositionTable::probe(Key key) const
    {
        const std::shared_lock lock{mutex_};
        const auto& entry = entries_[static_cast<std::size_t>(key % entries_.size())];
        if (entry.key == key && entry.bound != Bound::None)
        {
            return entry;
        }

        return std::nullopt;
    }

    void TranspositionTable::store(Key key, std::size_t depth, Score score, Bound bound, Move best_move)
    {
        const std::unique_lock lock{mutex_};
        auto& entry = entries_[static_cast<std::size_t>(key % entries_.size())];
        if (entry.bound != Bound::None && entry.key == key && entry.depth > depth && best_move == 0)
        {
            return;
        }

        entry = TranspositionEntry{
            key,   best_move,
            score, static_cast<std::uint16_t>(std::min<std::size_t>(depth, std::numeric_limits<std::uint16_t>::max())),
            bound,
        };
    }

} // namespace aurora::chess
