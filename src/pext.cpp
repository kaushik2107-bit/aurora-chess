#include "pext.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>

#if !defined(__BMI2__) && !defined(_MSC_VER)
#error "Aurora Chess now requires BMI2/PEXT support. Compile with -mbmi2 on x86_64 targets."
#endif

namespace aurora::chess
{
    namespace
    {

        // clang-format off
        constexpr std::array<int, 64> kSquareFiles = {
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
            0, 1, 2, 3, 4, 5, 6, 7,
        };

        constexpr std::array<int, 64> kSquareRanks = {
            0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1,
            2, 2, 2, 2, 2, 2, 2, 2,
            3, 3, 3, 3, 3, 3, 3, 3,
            4, 4, 4, 4, 4, 4, 4, 4,
            5, 5, 5, 5, 5, 5, 5, 5,
            6, 6, 6, 6, 6, 6, 6, 6,
            7, 7, 7, 7, 7, 7, 7, 7,
        };
        // clang-format on

        enum class Slider
        {
            Bishop,
            Rook,
        };

        constexpr std::array<int, 4> kRookFileSteps = {1, -1, 0, 0};
        constexpr std::array<int, 4> kRookRankSteps = {0, 0, 1, -1};

        constexpr std::array<int, 4> kBishopFileSteps = {1, 1, -1, -1};
        constexpr std::array<int, 4> kBishopRankSteps = {1, -1, 1, -1};

        constexpr bool is_on_board(int file, int rank) noexcept
        {
            return file >= 0 && file < 8 && rank >= 0 && rank < 8;
        }

        constexpr Bitboard square_bit(int file, int rank) noexcept
        {
            return static_cast<Bitboard>(1) << static_cast<std::size_t>(rank * 8 + file);
        }

        constexpr Bitboard ray_occupancy_mask(Square square, int file_step, int rank_step) noexcept
        {
            int file = kSquareFiles[static_cast<std::size_t>(square)];
            int rank = kSquareRanks[static_cast<std::size_t>(square)];
            Bitboard mask = 0;
            for (int x = file + file_step, y = rank + rank_step; is_on_board(x, y); x += file_step, y += rank_step)
            {
                if (!is_on_board(x + file_step, y + rank_step))
                {
                    break;
                }
                mask |= square_bit(x, y);
            }
            return mask;
        }

        [[nodiscard]] Bitboard ray_attacks(Square square, Bitboard occupancy, int file_step, int rank_step) noexcept
        {
            int file = kSquareFiles[static_cast<std::size_t>(square)];
            int rank = kSquareRanks[static_cast<std::size_t>(square)];
            Bitboard attacks = 0;
            for (int x = file + file_step, y = rank + rank_step; is_on_board(x, y); x += file_step, y += rank_step)
            {
                const Bitboard target = square_bit(x, y);
                attacks |= target;
                if ((occupancy & target) != 0)
                {
                    break;
                }
            }
            return attacks;
        }

        [[nodiscard]] Bitboard reference_rook_attacks(Square square, Bitboard occupancy) noexcept
        {
            Bitboard attacks = 0;
            for (std::size_t i = 0; i < kRookFileSteps.size(); ++i)
            {
                attacks |= ray_attacks(square, occupancy, kRookFileSteps[i], kRookRankSteps[i]);
            }
            return attacks;
        }

        [[nodiscard]] Bitboard reference_bishop_attacks(Square square, Bitboard occupancy) noexcept
        {
            Bitboard attacks = 0;
            for (std::size_t i = 0; i < kBishopFileSteps.size(); ++i)
            {
                attacks |= ray_attacks(square, occupancy, kBishopFileSteps[i], kBishopRankSteps[i]);
            }
            return attacks;
        }

        [[nodiscard]] Bitboard occupancy_from_table_index(std::size_t index, Bitboard mask) noexcept
        {
            Bitboard occupancy = 0;
            std::size_t bit_index = 0;
            while (mask != 0)
            {
                const Bitboard bit = mask & (0 - mask);
                if ((index & (std::size_t{1} << bit_index)) != 0)
                {
                    occupancy |= bit;
                }
                mask &= mask - 1;
                ++bit_index;
            }
            return occupancy;
        }

        void build_attack_table(Square square, Bitboard mask, std::vector<Bitboard>& table, Slider slider)
        {
            table.resize(std::size_t{1} << popcount(mask));
            for (std::size_t index = 0; index < table.size(); ++index)
            {
                const Bitboard occupancy = occupancy_from_table_index(index, mask);
                table[index] = slider == Slider::Rook ? reference_rook_attacks(square, occupancy) : reference_bishop_attacks(square, occupancy);
            }
        }

        [[nodiscard]] std::size_t attack_table_index(Bitboard occupancy, Bitboard mask) noexcept
        {
            return static_cast<std::size_t>(_pext_u64(occupancy, mask));
        }

    } // namespace

    bool SlidingAttacks::is_initialized_ = false;
    std::array<Bitboard, SlidingAttacks::kSquareCount> SlidingAttacks::rook_occupancy_masks_{};
    std::array<Bitboard, SlidingAttacks::kSquareCount> SlidingAttacks::bishop_occupancy_masks_{};
    std::array<std::vector<Bitboard>, SlidingAttacks::kSquareCount> SlidingAttacks::rook_attack_tables_{};
    std::array<std::vector<Bitboard>, SlidingAttacks::kSquareCount> SlidingAttacks::bishop_attack_tables_{};

    void SlidingAttacks::init()
    {
        if (is_initialized_)
        {
            return;
        }

        for (std::size_t square = 0; square < kSquareCount; ++square)
        {
            rook_occupancy_masks_[square] =
                ray_occupancy_mask(static_cast<Square>(square), 1, 0) |
                ray_occupancy_mask(static_cast<Square>(square), -1, 0) |
                ray_occupancy_mask(static_cast<Square>(square), 0, 1) |
                ray_occupancy_mask(static_cast<Square>(square), 0, -1);
            bishop_occupancy_masks_[square] =
                ray_occupancy_mask(static_cast<Square>(square), 1, 1) |
                ray_occupancy_mask(static_cast<Square>(square), 1, -1) |
                ray_occupancy_mask(static_cast<Square>(square), -1, 1) |
                ray_occupancy_mask(static_cast<Square>(square), -1, -1);

            build_attack_table(static_cast<Square>(square), rook_occupancy_masks_[square], rook_attack_tables_[square], Slider::Rook);
            build_attack_table(static_cast<Square>(square), bishop_occupancy_masks_[square], bishop_attack_tables_[square], Slider::Bishop);
        }

        is_initialized_ = true;
    }

    Bitboard SlidingAttacks::rook_attacks(Square square, Bitboard occupancy) noexcept
    {
        const auto index = static_cast<std::size_t>(square);
        return rook_attack_tables_[index][attack_table_index(occupancy, rook_occupancy_masks_[index])];
    }

    Bitboard SlidingAttacks::bishop_attacks(Square square, Bitboard occupancy) noexcept
    {
        const auto index = static_cast<std::size_t>(square);
        return bishop_attack_tables_[index][attack_table_index(occupancy, bishop_occupancy_masks_[index])];
    }

} // namespace aurora::chess
