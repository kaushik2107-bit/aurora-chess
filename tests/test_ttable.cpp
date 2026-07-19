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
        EXPECT_FALSE(entry->has_static_eval);
    }

    TEST(TranspositionTableTests, StoresStaticEvalWhenProvided)
    {
        aurora::chess::TranspositionTable table{16};
        constexpr aurora::chess::Key key = 0x4321;

        table.store(key, 3, 24, aurora::chess::Bound::Lower, 0, -12);

        const auto entry = table.probe(key);
        ASSERT_TRUE(entry.has_value());
        EXPECT_TRUE(entry->has_static_eval);
        EXPECT_EQ(entry->static_eval, -12);
    }

    TEST(TranspositionTableTests, StoresEvaluationWithoutSearchBound)
    {
        aurora::chess::TranspositionTable table{16};
        constexpr aurora::chess::Key key = 0x9876;

        table.store_static_eval(key, 31);

        const auto entry = table.probe(key);
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->bound, aurora::chess::Bound::None);
        EXPECT_TRUE(entry->has_static_eval);
        EXPECT_EQ(entry->static_eval, 31);
    }

    TEST(TranspositionTableTests, EvaluationOnlyStorePreservesCollidingBound)
    {
        aurora::chess::TranspositionTable table{1};
        constexpr aurora::chess::Key valuable_key = 1;
        constexpr aurora::chess::Move move =
            aurora::chess::make_move(aurora::chess::Square::E2, aurora::chess::Square::E4);
        table.store(valuable_key, 8, 42, aurora::chess::Bound::Exact, move);

        table.store_static_eval(2, -18);

        const auto entry = table.probe(valuable_key);
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->depth, 8);
        EXPECT_EQ(entry->score, 42);
        EXPECT_EQ(entry->best_move, move);
        EXPECT_EQ(entry->bound, aurora::chess::Bound::Exact);
    }

    TEST(TranspositionTableTests, MissesDifferentKeys)
    {
        aurora::chess::TranspositionTable table{16};
        table.store(0x1234, 5, 42, aurora::chess::Bound::Exact, 0);

        EXPECT_FALSE(table.probe(0x5678).has_value());
    }

    TEST(TranspositionTableTests, RetainsMultipleCollidingEntriesInCluster)
    {
        aurora::chess::TranspositionTable table{4};
        for (aurora::chess::Key key = 1; key <= 4; ++key)
        {
            table.store(key, key, -static_cast<aurora::chess::Score>(key), aurora::chess::Bound::Lower, 0);
        }
        for (aurora::chess::Key key = 1; key <= 4; ++key)
        {
            const auto entry = table.probe(key);
            ASSERT_TRUE(entry.has_value());
            EXPECT_EQ(entry->score, -static_cast<aurora::chess::Score>(key));
        }
    }

    TEST(TranspositionTableTests, ReplacesAgedClusterEntry)
    {
        aurora::chess::TranspositionTable table{4};
        for (aurora::chess::Key key = 1; key <= 4; ++key)
        {
            table.store(key, 8, 20, aurora::chess::Bound::Lower, 0);
        }
        for (int generation = 0; generation < 8; ++generation)
        {
            table.new_search();
        }

        table.store(5, 1, -7, aurora::chess::Bound::Upper, 0);

        const auto replacement = table.probe(5);
        ASSERT_TRUE(replacement.has_value());
        EXPECT_EQ(replacement->score, -7);
    }

    TEST(TranspositionTableTests, ReportsCurrentGenerationHashfullInPermille)
    {
        aurora::chess::TranspositionTable table{4};
        EXPECT_EQ(table.hashfull(), 0);

        table.store(1, 4, 12, aurora::chess::Bound::Lower, 0);
        EXPECT_EQ(table.hashfull(), 250);

        table.new_search();
        EXPECT_EQ(table.hashfull(), 0);
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
