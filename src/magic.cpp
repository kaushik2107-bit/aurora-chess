#include "magic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace aurora::chess
{
    namespace
    {

        constexpr std::array<int, 64> kFile = {
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
        };

        constexpr std::array<int, 64> kRank = {
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            2,
            3,
            3,
            3,
            3,
            3,
            3,
            3,
            3,
            4,
            4,
            4,
            4,
            4,
            4,
            4,
            4,
            5,
            5,
            5,
            5,
            5,
            5,
            5,
            5,
            6,
            6,
            6,
            6,
            6,
            6,
            6,
            6,
            7,
            7,
            7,
            7,
            7,
            7,
            7,
            7,
        };

        constexpr std::array<int, 4> kRookDirectionsX = {1, -1, 0, 0};
        constexpr std::array<int, 4> kRookDirectionsY = {0, 0, 1, -1};

        constexpr std::array<int, 4> kBishopDirectionsX = {1, 1, -1, -1};
        constexpr std::array<int, 4> kBishopDirectionsY = {1, -1, 1, -1};

        constexpr bool in_bounds(int file, int rank) noexcept
        {
            return file >= 0 && file < 8 && rank >= 0 && rank < 8;
        }

        constexpr Bitboard ray_mask(Square square, int dx, int dy) noexcept
        {
            int file = kFile[static_cast<std::size_t>(square)];
            int rank = kRank[static_cast<std::size_t>(square)];
            Bitboard mask = 0;
            for (int x = file + dx, y = rank + dy; in_bounds(x, y); x += dx, y += dy)
            {
                mask |= static_cast<Bitboard>(1) << (y * 8 + x);
            }
            return mask;
        }

        [[nodiscard]] Bitboard rays_from(Square square, Bitboard occupancy, int dx, int dy) noexcept
        {
            int file = kFile[static_cast<std::size_t>(square)];
            int rank = kRank[static_cast<std::size_t>(square)];
            Bitboard attacks = 0;
            for (int x = file + dx, y = rank + dy; in_bounds(x, y); x += dx, y += dy)
            {
                const auto target = static_cast<Square>(y * 8 + x);
                attacks |= static_cast<Bitboard>(1) << static_cast<std::size_t>(target);
                if ((occupancy & (static_cast<Bitboard>(1) << static_cast<std::size_t>(target))) != 0)
                {
                    break;
                }
            }
            return attacks;
        }

    } // namespace

    bool MagicBitboards::initialized_ = false;
    std::array<Bitboard, MagicBitboards::kBoardSize> MagicBitboards::rook_masks_{};
    std::array<Bitboard, MagicBitboards::kBoardSize> MagicBitboards::bishop_masks_{};
    std::array<Bitboard, MagicBitboards::kBoardSize> MagicBitboards::rook_table_{};
    std::array<Bitboard, MagicBitboards::kBoardSize> MagicBitboards::bishop_table_{};

    void MagicBitboards::init()
    {
        if (initialized_)
        {
            return;
        }

        for (std::size_t square = 0; square < kBoardSize; ++square)
        {
            rook_masks_[square] = ray_mask(static_cast<Square>(square), 1, 0) | ray_mask(static_cast<Square>(square), -1, 0) |
                                  ray_mask(static_cast<Square>(square), 0, 1) | ray_mask(static_cast<Square>(square), 0, -1);
            bishop_masks_[square] = ray_mask(static_cast<Square>(square), 1, 1) | ray_mask(static_cast<Square>(square), 1, -1) |
                                    ray_mask(static_cast<Square>(square), -1, 1) | ray_mask(static_cast<Square>(square), -1, -1);
        }

        initialized_ = true;
    }

    Bitboard MagicBitboards::rook_moves(Square square, Bitboard occupancy) noexcept
    {
        init();
        Bitboard attacks = 0;
        for (std::size_t i = 0; i < kRookDirectionsX.size(); ++i)
        {
            attacks |= rays_from(square, occupancy, kRookDirectionsX[i], kRookDirectionsY[i]);
        }
        return attacks;
    }

    Bitboard MagicBitboards::bishop_moves(Square square, Bitboard occupancy) noexcept
    {
        init();
        Bitboard attacks = 0;
        for (std::size_t i = 0; i < kBishopDirectionsX.size(); ++i)
        {
            attacks |= rays_from(square, occupancy, kBishopDirectionsX[i], kBishopDirectionsY[i]);
        }
        return attacks;
    }

} // namespace aurora::chess
