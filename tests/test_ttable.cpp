#include "ttable.hpp"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace
{

    TEST(TranspositionTableTests, StoresAndProbesEntries)
    {
        aurora::chess::TranspositionTable table{16};
        constexpr aurora::chess::Key key = 0x1234;
        constexpr aurora::chess::Move move =
            aurora::chess::make_move(aurora::chess::Square::E2, aurora::chess::Square::E4);

        table.store(key, 5, 42, aurora::chess::Bound::Exact, move);

        const auto entry = table.probe(key);
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->key, key);
        EXPECT_EQ(entry->depth, 5);
        EXPECT_EQ(entry->score, 42);
        EXPECT_EQ(entry->bound, aurora::chess::Bound::Exact);
        EXPECT_EQ(entry->best_move, move);
    }

    TEST(TranspositionTableTests, MissesDifferentKeys)
    {
        aurora::chess::TranspositionTable table{16};
        table.store(0x1234, 5, 42, aurora::chess::Bound::Exact, 0);

        EXPECT_FALSE(table.probe(0x5678).has_value());
    }

    TEST(TranspositionTableTests, SupportsConcurrentStoreAndProbe)
    {
        aurora::chess::TranspositionTable table{1024};
        std::atomic_bool consistent{true};
        std::vector<std::thread> workers;

        for (std::uint64_t thread = 0; thread < 4; ++thread)
        {
            workers.emplace_back(
                [&table, &consistent, thread]()
                {
                    for (std::uint64_t index = 0; index < 1000; ++index)
                    {
                        const aurora::chess::Key key = 1 + thread * 10'000 + index;
                        table.store(key, 4, 25, aurora::chess::Bound::Lower, 0);

                        const auto entry = table.probe(key);
                        if (entry.has_value() && entry->key != key)
                        {
                            consistent = false;
                        }
                    }
                });
        }

        for (auto& worker : workers)
        {
            worker.join();
        }

        EXPECT_TRUE(consistent.load());
    }

} // namespace
