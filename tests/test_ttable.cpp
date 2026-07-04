#include "ttable.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(TranspositionTableTests, StoresAndProbesEntries)
    {
        aurora::chess::TranspositionTable table{16};
        constexpr aurora::chess::Key key = 0x1234;
        constexpr aurora::chess::Move move = aurora::chess::make_move(aurora::chess::Square::E2, aurora::chess::Square::E4);

        table.store(key, 5, 42, aurora::chess::Bound::Exact, move);

        const auto *entry = table.probe(key);
        ASSERT_NE(entry, nullptr);
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

        EXPECT_EQ(table.probe(0x5678), nullptr);
    }

} // namespace
