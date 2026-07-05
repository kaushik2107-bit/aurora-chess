#include "engine.hpp"
#include "ucioptions.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace
{

    TEST(UciOptionsTests, ParsesSetOptionCaseInsensitively)
    {
        const aurora::chess::UciOptions options;
        const auto command = options.parse_setoption("setoption NAME Ponder VALUE true");

        ASSERT_TRUE(command.has_value());
        EXPECT_EQ(command->name, "Ponder");
        EXPECT_EQ(command->value, "true");
    }

    TEST(UciOptionsTests, AppliesPonderOption)
    {
        aurora::chess::Engine engine{"Aurora"};
        aurora::chess::UciOptions options;
        const auto command = options.parse_setoption("setoption name Ponder value true");

        ASSERT_TRUE(command.has_value());
        EXPECT_TRUE(options.apply(engine, *command));
        EXPECT_TRUE(options.ponder());
    }

    TEST(UciOptionsTests, AppliesHashAndThreadOptions)
    {
        aurora::chess::Engine engine{"Aurora"};
        aurora::chess::UciOptions options;

        const auto hash = options.parse_setoption("setoption name Hash value 32");
        ASSERT_TRUE(hash.has_value());
        EXPECT_TRUE(options.apply(engine, *hash));
        EXPECT_EQ(options.hash_mb(), 32u);

        const auto threads = options.parse_setoption("setoption name Threads value 2");
        ASSERT_TRUE(threads.has_value());
        EXPECT_TRUE(options.apply(engine, *threads));
        EXPECT_EQ(engine.thread_count(), 2u);

        const auto overhead = options.parse_setoption("setoption name Move Overhead value 75");
        ASSERT_TRUE(overhead.has_value());
        EXPECT_TRUE(options.apply(engine, *overhead));
        EXPECT_EQ(options.move_overhead_ms(), 75);
    }

    TEST(UciOptionsTests, AppliesUseNnueOption)
    {
        aurora::chess::Engine engine{"Aurora"};
        aurora::chess::UciOptions options;
        const auto command = options.parse_setoption("setoption name Use NNUE value true");

        ASSERT_TRUE(command.has_value());
        EXPECT_TRUE(options.apply(engine, *command));
        EXPECT_TRUE(engine.use_nnue());
    }

    TEST(UciOptionsTests, RejectsInvalidTypedValues)
    {
        aurora::chess::Engine engine{"Aurora"};
        aurora::chess::UciOptions options;

        const auto hash = options.parse_setoption("setoption name Hash value 9999");
        ASSERT_TRUE(hash.has_value());

        EXPECT_FALSE(options.apply(engine, *hash));
        EXPECT_EQ(options.hash_mb(), 16u);
    }

    TEST(UciOptionsTests, AppliesButtonOptionWithoutValue)
    {
        aurora::chess::Engine engine{"Aurora"};
        aurora::chess::UciOptions options;
        const auto command = options.parse_setoption("setoption name Clear Hash");

        ASSERT_TRUE(command.has_value());
        EXPECT_TRUE(options.apply(engine, *command));
    }

    TEST(UciOptionsTests, WritesAvailableOptions)
    {
        aurora::chess::Engine engine{"Aurora"};
        aurora::chess::UciOptions options;
        std::ostringstream output;

        options.write(output, engine);

        EXPECT_NE(output.str().find("option name Hash type spin"), std::string::npos);
        EXPECT_NE(output.str().find("option name Ponder type check"), std::string::npos);
        EXPECT_NE(output.str().find("option name Move Overhead type spin"), std::string::npos);
        EXPECT_NE(output.str().find("option name Use NNUE type check"), std::string::npos);
        EXPECT_NE(output.str().find("option name Threads type spin"), std::string::npos);
    }

} // namespace
