#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "evaluation.hpp"

namespace aurora::chess
{

    class NnueEvaluator final : public Evaluator
    {
    public:
        static constexpr std::size_t kAccumulatorSize = 128;

        struct Accumulator
        {
            std::array<std::array<float, kAccumulatorSize>, 2> values{};
            std::array<Square, 2> king_squares{Square::NoSquare, Square::NoSquare};
            bool valid{false};
        };

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
        [[nodiscard]] Score evaluate(const Board& board, const Accumulator& accumulator) const noexcept;

        void refresh_accumulator(const Board& board, Accumulator& accumulator) const noexcept;
        void update_accumulator(const Accumulator& previous, const Board& board, const Board::DirtyPiece& dirty,
                                Accumulator& next) const noexcept;

    private:
        struct Network;

        void apply_feature(Accumulator& accumulator, Color perspective, Color piece_color, PieceType piece_type,
                           Square square, float sign) const noexcept;

        std::unique_ptr<Network> network_;
        PsqtEvaluator fallback_;
        std::string path_;
    };

} // namespace aurora::chess
