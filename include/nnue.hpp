#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "evaluation.hpp"

namespace aurora::chess
{

    class NnueEvaluator final : public Evaluator
    {
    public:
        NnueEvaluator();
        ~NnueEvaluator() override;

        NnueEvaluator(const NnueEvaluator&) = delete;
        NnueEvaluator& operator=(const NnueEvaluator&) = delete;
        NnueEvaluator(NnueEvaluator&&) noexcept;
        NnueEvaluator& operator=(NnueEvaluator&&) noexcept;

        [[nodiscard]] bool load(std::string_view path);
        [[nodiscard]] bool load_default();

        [[nodiscard]] bool is_loaded() const noexcept;
        [[nodiscard]] std::string_view path() const noexcept;
        [[nodiscard]] Score evaluate(const Board& board) const noexcept override;

    private:
        struct Network;

        std::unique_ptr<Network> network_;
        PsqtEvaluator fallback_;
        std::string path_;
    };

} // namespace aurora::chess
