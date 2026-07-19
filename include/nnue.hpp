#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "evaluation.hpp"

namespace aurora::chess
{

    class NnueEvaluator final : public Evaluator
    {
    public:
        static constexpr std::size_t kAccumulatorSize = 3072;

        struct Accumulator
        {
            alignas(64) std::array<std::array<std::int16_t, kAccumulatorSize>, 2> values{};
            std::array<std::array<std::int32_t, 8>, 2> psqt{};
            std::array<Square, 2> king_squares{Square::NoSquare, Square::NoSquare};
            bool valid{false};
        };

        struct RefreshCacheEntry
        {
            alignas(64) std::array<std::int16_t, kAccumulatorSize> values{};
            std::array<std::int32_t, 8> psqt{};
            std::array<std::uint64_t, 11> features{};
            bool valid{false};
        };

        struct RefreshCache
        {
            std::array<std::array<RefreshCacheEntry, 32>, 2> entries{};
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

        void refresh_accumulator(const Board& board, Accumulator& accumulator,
                                 RefreshCache* cache = nullptr) const noexcept;
        void update_accumulator(const Accumulator& previous, const Board& board, const Board::DirtyPiece& dirty,
                                Accumulator& next, RefreshCache* cache = nullptr) const noexcept;

    private:
        struct Network;

        void apply_feature(Accumulator& accumulator, Color perspective, Color piece_color, PieceType piece_type,
                           Square square, int sign) const noexcept;
        void refresh_perspective(const Board& board, Color perspective, Accumulator& accumulator,
                                 RefreshCache* cache) const noexcept;

        std::unique_ptr<Network> network_;
        PsqtEvaluator fallback_;
        std::string path_;
    };

} // namespace aurora::chess
