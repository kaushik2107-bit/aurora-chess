#pragma once

#include "board.hpp"

namespace aurora::chess
{

    using Score = int;

    class Evaluator
    {
    public:
        virtual ~Evaluator() = default;
        [[nodiscard]] virtual Score evaluate(const Board &board) const noexcept = 0;
    };

    class PsqtEvaluator final : public Evaluator
    {
    public:
        [[nodiscard]] Score evaluate(const Board &board) const noexcept override;
    };

    [[nodiscard]] const Evaluator &default_evaluator() noexcept;

} // namespace aurora::chess
