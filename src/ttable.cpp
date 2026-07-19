#include "ttable.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace aurora::chess
{
    namespace
    {
        constexpr std::uint64_t kSignatureMask = (std::uint64_t{1} << 63) - 1;
        constexpr std::uint64_t kWriteLock = std::uint64_t{1} << 63;
        constexpr unsigned kStaticEvalShift = 16;
        constexpr unsigned kMoveShift = 32;
        constexpr unsigned kDepthShift = 48;
        constexpr unsigned kBoundShift = 56;
        constexpr unsigned kHasEvalShift = 58;
        constexpr unsigned kGenerationShift = 59;
        constexpr std::uint8_t kGenerationMask = 0x1f;

        [[nodiscard]] std::uint16_t pack_score(Score score) noexcept
        {
            return std::bit_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp<Score>(
                score, std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max())));
        }

        [[nodiscard]] Score unpack_score(std::uint64_t value) noexcept
        {
            return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
        }

        [[nodiscard]] std::uint64_t pack_data(const TranspositionEntry& entry, std::uint8_t generation) noexcept
        {
            return static_cast<std::uint64_t>(pack_score(entry.score)) |
                   (static_cast<std::uint64_t>(pack_score(entry.static_eval)) << kStaticEvalShift) |
                   (static_cast<std::uint64_t>(entry.best_move) << kMoveShift) |
                   (static_cast<std::uint64_t>(std::min<std::uint16_t>(entry.depth, 255)) << kDepthShift) |
                   (static_cast<std::uint64_t>(entry.bound) << kBoundShift) |
                   (static_cast<std::uint64_t>(entry.has_static_eval) << kHasEvalShift) |
                   (static_cast<std::uint64_t>(generation & kGenerationMask) << kGenerationShift);
        }

        [[nodiscard]] int replacement_value(const TranspositionEntry& entry, std::uint8_t entry_generation,
                                            std::uint8_t current_generation) noexcept
        {
            if (entry.bound == Bound::None && !entry.has_static_eval)
            {
                return std::numeric_limits<int>::min();
            }
            const unsigned age = (current_generation - entry_generation) & kGenerationMask;
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
            const std::uint64_t before = source.signature.load(std::memory_order_acquire);
            if ((before & kWriteLock) != 0)
            {
                continue;
            }
            const std::uint64_t data = source.data.load(std::memory_order_relaxed);
            const std::uint64_t after = source.signature.load(std::memory_order_acquire);
            if (before != after)
            {
                continue;
            }
            destination = TranspositionEntry{
                before & kSignatureMask,
                static_cast<Move>((data >> kMoveShift) & 0xffffU),
                unpack_score(data),
                unpack_score(data >> kStaticEvalShift),
                static_cast<std::uint16_t>((data >> kDepthShift) & 0xffU),
                static_cast<Bound>((data >> kBoundShift) & 0x3U),
                ((data >> kHasEvalShift) & 1U) != 0,
            };
            generation = static_cast<std::uint8_t>((data >> kGenerationShift) & kGenerationMask);
            return true;
        }
        return false;
    }

    void TranspositionTable::write_entry(AtomicEntry& destination, const TranspositionEntry& source,
                                         std::uint8_t generation) noexcept
    {
        std::uint64_t signature = destination.signature.load(std::memory_order_relaxed);
        for (;;)
        {
            if ((signature & kWriteLock) == 0 && destination.signature.compare_exchange_weak(
                                                    signature, signature | kWriteLock,
                                                    std::memory_order_acquire, std::memory_order_relaxed))
            {
                break;
            }
            signature = destination.signature.load(std::memory_order_relaxed);
        }
        destination.data.store(pack_data(source, generation), std::memory_order_relaxed);
        destination.signature.store(source.key & kSignatureMask, std::memory_order_release);
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
        generation_.store(static_cast<std::uint8_t>((generation_.load(std::memory_order_relaxed) + 1) &
                                                    kGenerationMask),
                          std::memory_order_relaxed);
    }

    std::size_t TranspositionTable::entry_count() const
    {
        return cluster_count_ * kClusterSize;
    }

    std::size_t TranspositionTable::hashfull() const noexcept
    {
        const std::size_t sampled_clusters = std::min<std::size_t>(cluster_count_, 250);
        const std::size_t sampled_entries = sampled_clusters * kClusterSize;
        const auto current_generation = generation_.load(std::memory_order_relaxed) & kGenerationMask;
        std::size_t used = 0;
        for (std::size_t cluster = 0; cluster < sampled_clusters; ++cluster)
        {
            for (const auto& source : clusters_[cluster].entries)
            {
                TranspositionEntry entry;
                std::uint8_t entry_generation = 0;
                if (read_entry(source, entry, entry_generation) && entry_generation == current_generation &&
                    (entry.bound != Bound::None || entry.has_static_eval))
                {
                    ++used;
                }
            }
        }
        return sampled_entries == 0 ? 0 : used * 1000 / sampled_entries;
    }

    std::optional<TranspositionEntry> TranspositionTable::probe(Key key) const
    {
        const auto& cluster = clusters_[static_cast<std::size_t>(key % cluster_count_)];
        const Key signature = key & kSignatureMask;
        for (const auto& source : cluster.entries)
        {
            TranspositionEntry entry;
            std::uint8_t generation = 0;
            if (read_entry(source, entry, generation) && entry.key == signature &&
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
        const Key signature = key & kSignatureMask;
        AtomicEntry* empty = nullptr;
        for (auto& destination : cluster.entries)
        {
            TranspositionEntry entry;
            std::uint8_t generation = 0;
            if (!read_entry(destination, entry, generation))
            {
                continue;
            }
            if (entry.key == signature)
            {
                entry.key = key;
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
        const Key signature = key & kSignatureMask;
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
            if (entry.key == signature)
            {
                if (entry.depth > depth && best_move == 0)
                {
                    if (static_eval)
                    {
                        entry.key = key;
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
        if (best_move == 0 && target_entry.key == signature)
        {
            best_move = target_entry.best_move;
        }
        write_entry(*target,
                    TranspositionEntry{
                        key,
                        best_move,
                        score,
                        static_eval.value_or(0),
                        static_cast<std::uint16_t>(std::min<std::size_t>(depth, 255)),
                        bound,
                        static_eval.has_value(),
                    },
                    current_generation);
    }

} // namespace aurora::chess
