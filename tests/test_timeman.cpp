#include "timeman.hpp"

#include <gtest/gtest.h>

namespace
{

    TEST(TimeManagerTests, UsesFixedMoveTimeDirectly)
    {
        const aurora::chess::TimePlan plan = aurora::chess::plan_time(aurora::chess::TimeControl{.move_time_ms = 250},
                                                                      aurora::chess::Color::White, false);

        EXPECT_TRUE(plan.active);
        EXPECT_EQ(plan.optimum.count(), 250);
        EXPECT_EQ(plan.maximum.count(), 250);
    }

    TEST(TimeManagerTests, PlansConservativeClockBudget)
    {
        aurora::chess::TimeControl control;
        control.white_time_ms = 30'000;
        control.black_time_ms = 20'000;
        control.white_increment_ms = 500;
        control.black_increment_ms = 0;

        const aurora::chess::TimePlan plan = aurora::chess::plan_time(control, aurora::chess::Color::White, false);

        EXPECT_TRUE(plan.active);
        EXPECT_GT(plan.optimum.count(), 0);
        EXPECT_GE(plan.maximum, plan.optimum);
        EXPECT_LT(plan.maximum.count(), 30'000);
    }

    TEST(TimeManagerTests, UsesMovesToGoForSessionTimeControl)
    {
        aurora::chess::TimeControl control;
        control.black_time_ms = 10'000;
        control.moves_to_go = 10;

        const aurora::chess::TimePlan plan = aurora::chess::plan_time(control, aurora::chess::Color::Black, false);

        EXPECT_TRUE(plan.active);
        EXPECT_NEAR(plan.optimum.count(), 998, 5);
        EXPECT_GE(plan.maximum, plan.optimum);
    }

    TEST(TimeManagerTests, AppliesMoveOverhead)
    {
        aurora::chess::TimeControl low_overhead;
        low_overhead.white_time_ms = 10'000;
        low_overhead.moves_to_go = 10;
        low_overhead.move_overhead_ms = 0;

        aurora::chess::TimeControl high_overhead = low_overhead;
        high_overhead.move_overhead_ms = 1'000;

        const aurora::chess::TimePlan low = aurora::chess::plan_time(low_overhead, aurora::chess::Color::White, false);
        const aurora::chess::TimePlan high =
            aurora::chess::plan_time(high_overhead, aurora::chess::Color::White, false);

        EXPECT_TRUE(low.active);
        EXPECT_TRUE(high.active);
        EXPECT_LT(high.optimum, low.optimum);
        EXPECT_LT(high.maximum, low.maximum);
    }

    TEST(TimeManagerTests, ReturnsInactiveWithoutClock)
    {
        const aurora::chess::TimePlan plan =
            aurora::chess::plan_time(aurora::chess::TimeControl{}, aurora::chess::Color::White, false);

        EXPECT_FALSE(plan.active);
    }

} // namespace
