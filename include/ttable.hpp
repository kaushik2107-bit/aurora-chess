#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <array>
#include <memory>
#include <optional>

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
        static constexpr std::size_t kEntryBytes = 16;

        explicit TranspositionTable(std::size_t entry_count = 1 << 20);

        void clear();
        void resize(std::size_t entry_count);
        void new_search() noexcept;
        [[nodiscard]] std::size_t entry_count() const;
        [[nodiscard]] std::size_t hashfull() const noexcept;
        [[nodiscard]] std::optional<TranspositionEntry> probe(Key key) const;
        void store(Key key, std::size_t depth, Score score, Bound bound, Move best_move,
                   std::optional<Score> static_eval = std::nullopt);
        void store_static_eval(Key key, Score static_eval);

    private:
        static constexpr std::size_t kClusterSize = 4;

        struct AtomicEntry
        {
            std::atomic<std::uint64_t> data{0};
            std::atomic<std::uint64_t> signature{0};
        };
        static_assert(sizeof(AtomicEntry) == kEntryBytes);

        struct alignas(64) Cluster
        {
            std::array<AtomicEntry, kClusterSize> entries{};
        };

        [[nodiscard]] bool read_entry(const AtomicEntry& source, TranspositionEntry& destination,
                                      std::uint8_t& generation) const noexcept;
        void write_entry(AtomicEntry& destination, const TranspositionEntry& source,
                         std::uint8_t generation) noexcept;

        std::unique_ptr<Cluster[]> clusters_;
        std::size_t cluster_count_{1};
        std::atomic<std::uint8_t> generation_{0};
    };

} // namespace aurora::chess
