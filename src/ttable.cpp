#include "ttable.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace aurora::chess
{
    namespace
    {
        constexpr std::uint64_t kMoveMask = 0xffffULL;
        constexpr unsigned kDepthShift = 16;
        constexpr unsigned kBoundShift = 32;
        constexpr unsigned kEvalShift = 34;
        constexpr unsigned kGenerationShift = 35;

        [[nodiscard]] std::uint64_t pack_scores(Score score, Score static_eval) noexcept
        {
            return static_cast<std::uint32_t>(score) |
                   (static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_eval)) << 32);
        }

        [[nodiscard]] std::uint64_t pack_metadata(const TranspositionEntry& entry, std::uint8_t generation) noexcept
        {
            return static_cast<std::uint64_t>(entry.best_move) |
                   (static_cast<std::uint64_t>(entry.depth) << kDepthShift) |
                   (static_cast<std::uint64_t>(entry.bound) << kBoundShift) |
                   (static_cast<std::uint64_t>(entry.has_static_eval) << kEvalShift) |
                   (static_cast<std::uint64_t>(generation) << kGenerationShift);
        }

        [[nodiscard]] int replacement_value(const TranspositionEntry& entry, std::uint8_t entry_generation,
                                            std::uint8_t current_generation) noexcept
        {
            if (entry.bound == Bound::None && !entry.has_static_eval)
            {
                return std::numeric_limits<int>::min();
            }
            const unsigned age = static_cast<std::uint8_t>(current_generation - entry_generation);
            const int bound_bonus = entry.bound == Bound::Exact ? 12 : entry.bound == Bound::None ? -16 : 4;
            return static_cast<int>(entry.depth) * 4 + bound_bonus - static_cast<int>(age) * 8;
        }
    }

    TranspositionTable::TranspositionTable(std::size_t entry_count)
    {
        resize(entry_count);
    }

    bool TranspositionTable::read_entry(const AtomicEntry& source, TranspositionEntry& destination,
                                        std::uint8_t& generation) const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const std::uint64_t before = source.sequence.load(std::memory_order_acquire);
            if ((before & 1U) != 0)
            {
                continue;
            }
            const Key key = source.key.load(std::memory_order_relaxed);
            const std::uint64_t scores = source.scores.load(std::memory_order_relaxed);
            const std::uint64_t metadata = source.metadata.load(std::memory_order_relaxed);
            const std::uint64_t after = source.sequence.load(std::memory_order_acquire);
            if (before != after)
            {
                continue;
            }
            destination = TranspositionEntry{
                key,
                static_cast<Move>(metadata & kMoveMask),
                std::bit_cast<Score>(static_cast<std::uint32_t>(scores)),
                std::bit_cast<Score>(static_cast<std::uint32_t>(scores >> 32)),
                static_cast<std::uint16_t>((metadata >> kDepthShift) & 0xffffU),
                static_cast<Bound>((metadata >> kBoundShift) & 0x3U),
                ((metadata >> kEvalShift) & 1U) != 0,
            };
            generation = static_cast<std::uint8_t>((metadata >> kGenerationShift) & 0xffU);
            return true;
        }
        return false;
    }

    void TranspositionTable::write_entry(AtomicEntry& destination, const TranspositionEntry& source,
                                         std::uint8_t generation) noexcept
    {
        std::uint64_t sequence = destination.sequence.load(std::memory_order_relaxed);
        for (;;)
        {
            if ((sequence & 1U) == 0 && destination.sequence.compare_exchange_weak(
                                             sequence, sequence + 1, std::memory_order_acquire,
                                             std::memory_order_relaxed))
            {
                break;
            }
            sequence = destination.sequence.load(std::memory_order_relaxed);
        }
        destination.key.store(source.key, std::memory_order_relaxed);
        destination.scores.store(pack_scores(source.score, source.static_eval), std::memory_order_relaxed);
        destination.metadata.store(pack_metadata(source, generation), std::memory_order_relaxed);
        destination.sequence.store(sequence + 2, std::memory_order_release);
    }

    void TranspositionTable::clear()
    {
        for (std::size_t cluster = 0; cluster < cluster_count_; ++cluster)
        {
            for (auto& entry : clusters_[cluster].entries)
            {
                write_entry(entry, TranspositionEntry{}, 0);
            }
        }
        generation_.store(0, std::memory_order_relaxed);
    }

    void TranspositionTable::resize(std::size_t entry_count)
    {
        cluster_count_ = std::max<std::size_t>(1, (entry_count + kClusterSize - 1) / kClusterSize);
        clusters_ = std::make_unique<Cluster[]>(cluster_count_);
        generation_.store(0, std::memory_order_relaxed);
    }

    void TranspositionTable::new_search() noexcept
    {
        generation_.fetch_add(1, std::memory_order_relaxed);
    }

    std::size_t TranspositionTable::entry_count() const
    {
        return cluster_count_ * kClusterSize;
    }

    std::optional<TranspositionEntry> TranspositionTable::probe(Key key) const
    {
        const auto& cluster = clusters_[static_cast<std::size_t>(key % cluster_count_)];
        for (const auto& source : cluster.entries)
        {
            TranspositionEntry entry;
            std::uint8_t generation = 0;
            if (read_entry(source, entry, generation) && entry.key == key &&
                (entry.bound != Bound::None || entry.has_static_eval))
            {
                return entry;
            }
        }
        return std::nullopt;
    }

    void TranspositionTable::store_static_eval(Key key, Score static_eval)
    {
        auto& cluster = clusters_[static_cast<std::size_t>(key % cluster_count_)];
        AtomicEntry* empty = nullptr;
        for (auto& destination : cluster.entries)
        {
            TranspositionEntry entry;
            std::uint8_t generation = 0;
            if (!read_entry(destination, entry, generation))
            {
                continue;
            }
            if (entry.key == key)
            {
                entry.static_eval = static_eval;
                entry.has_static_eval = true;
                write_entry(destination, entry, generation_.load(std::memory_order_relaxed));
                return;
            }
            if (empty == nullptr && entry.bound == Bound::None && !entry.has_static_eval)
            {
                empty = &destination;
            }
        }
        if (empty != nullptr)
        {
            write_entry(*empty, TranspositionEntry{key, 0, 0, static_eval, 0, Bound::None, true},
                        generation_.load(std::memory_order_relaxed));
        }
    }

    void TranspositionTable::store(Key key, std::size_t depth, Score score, Bound bound, Move best_move,
                                   std::optional<Score> static_eval)
    {
        auto& cluster = clusters_[static_cast<std::size_t>(key % cluster_count_)];
        const auto current_generation = generation_.load(std::memory_order_relaxed);
        AtomicEntry* target = nullptr;
        TranspositionEntry target_entry;
        int lowest_value = std::numeric_limits<int>::max();

        for (auto& destination : cluster.entries)
        {
            TranspositionEntry entry;
            std::uint8_t entry_generation = 0;
            if (!read_entry(destination, entry, entry_generation))
            {
                continue;
            }
            if (entry.key == key)
            {
                if (entry.depth > depth && best_move == 0)
                {
                    if (static_eval)
                    {
                        entry.static_eval = *static_eval;
                        entry.has_static_eval = true;
                        write_entry(destination, entry, current_generation);
                    }
                    return;
                }
                target = &destination;
                target_entry = entry;
                break;
            }
            const int value = replacement_value(entry, entry_generation, current_generation);
            if (value < lowest_value)
            {
                lowest_value = value;
                target = &destination;
                target_entry = entry;
            }
        }
        if (target == nullptr)
        {
            return;
        }
        if (best_move == 0 && target_entry.key == key)
        {
            best_move = target_entry.best_move;
        }
        write_entry(*target,
                    TranspositionEntry{
                        key,
                        best_move,
                        score,
                        static_eval.value_or(0),
                        static_cast<std::uint16_t>(std::min<std::size_t>(
                            depth, std::numeric_limits<std::uint16_t>::max())),
                        bound,
                        static_eval.has_value(),
                    },
                    current_generation);
    }

} // namespace aurora::chess
