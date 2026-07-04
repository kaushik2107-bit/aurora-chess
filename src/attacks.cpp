#include "attacks.hpp"

#include "bitboard.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>

namespace aurora::chess
{
    namespace
    {
        constexpr std::size_t kSquareCount = 64;
        constexpr std::size_t kRookAttackTableSize = 4096;
        constexpr std::size_t kBishopAttackTableSize = 512;

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

        constexpr Bitboard ray_attacks(Square square, Bitboard occupancy, int file_step, int rank_step) noexcept
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

        constexpr Bitboard reference_rook_attacks(Square square, Bitboard occupancy) noexcept
        {
            Bitboard attacks = 0;
            for (std::size_t i = 0; i < kRookFileSteps.size(); ++i)
            {
                attacks |= ray_attacks(square, occupancy, kRookFileSteps[i], kRookRankSteps[i]);
            }
            return attacks;
        }

        constexpr Bitboard reference_bishop_attacks(Square square, Bitboard occupancy) noexcept
        {
            Bitboard attacks = 0;
            for (std::size_t i = 0; i < kBishopFileSteps.size(); ++i)
            {
                attacks |= ray_attacks(square, occupancy, kBishopFileSteps[i], kBishopRankSteps[i]);
            }
            return attacks;
        }

        constexpr Bitboard occupancy_from_table_index(std::size_t index, Bitboard mask) noexcept
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

        template <std::size_t TableSize>
        void build_attack_table(Square square, Bitboard mask, std::array<Bitboard, TableSize>& table, Slider slider)
        {
            const std::size_t entry_count = std::size_t{1} << popcount(mask);
            for (std::size_t index = 0; index < entry_count; ++index)
            {
                const Bitboard occupancy = occupancy_from_table_index(index, mask);
                table[index] = slider == Slider::Rook ? reference_rook_attacks(square, occupancy)
                                                      : reference_bishop_attacks(square, occupancy);
            }
        }

        [[nodiscard]] std::size_t attack_table_index(Bitboard occupancy, Bitboard mask) noexcept
        {
            return static_cast<std::size_t>(_pext_u64(occupancy, mask));
        }

        static const std::array<Bitboard, kSquareCount> rook_occupancy_masks = []()
        {
            std::array<Bitboard, kSquareCount> masks{};
            for (std::size_t square = 0; square < kSquareCount; ++square)
            {
                masks[square] = ray_occupancy_mask(static_cast<Square>(square), 1, 0) |
                                ray_occupancy_mask(static_cast<Square>(square), -1, 0) |
                                ray_occupancy_mask(static_cast<Square>(square), 0, 1) |
                                ray_occupancy_mask(static_cast<Square>(square), 0, -1);
            }
            return masks;
        }();

        static const std::array<Bitboard, kSquareCount> bishop_occupancy_masks = []()
        {
            std::array<Bitboard, kSquareCount> masks{};
            for (std::size_t square = 0; square < kSquareCount; ++square)
            {
                masks[square] = ray_occupancy_mask(static_cast<Square>(square), 1, 1) |
                                ray_occupancy_mask(static_cast<Square>(square), 1, -1) |
                                ray_occupancy_mask(static_cast<Square>(square), -1, 1) |
                                ray_occupancy_mask(static_cast<Square>(square), -1, -1);
            }
            return masks;
        }();

        static const std::array<std::array<Bitboard, kRookAttackTableSize>, kSquareCount> rook_attack_tables = []()
        {
            std::array<std::array<Bitboard, kRookAttackTableSize>, kSquareCount> tables{};
            for (std::size_t square = 0; square < kSquareCount; ++square)
            {
                build_attack_table(static_cast<Square>(square), rook_occupancy_masks[square], tables[square],
                                   Slider::Rook);
            }
            return tables;
        }();

        static const std::array<std::array<Bitboard, kBishopAttackTableSize>, kSquareCount> bishop_attack_tables = []()
        {
            std::array<std::array<Bitboard, kBishopAttackTableSize>, kSquareCount> tables{};
            for (std::size_t square = 0; square < kSquareCount; ++square)
            {
                build_attack_table(static_cast<Square>(square), bishop_occupancy_masks[square], tables[square],
                                   Slider::Bishop);
            }
            return tables;
        }();

        constexpr auto kPawnAttacks = []() constexpr
        {
            std::array<std::array<Bitboard, 2>, 64> table{};
            for (std::size_t i = 0; i < 64; ++i)
            {
                const Square square = static_cast<Square>(i);
                const Bitboard pawn = bit(square);
                table[i][static_cast<std::size_t>(Color::White)] = ((pawn & ~kFileA) << 7) | ((pawn & ~kFileH) << 9);
                table[i][static_cast<std::size_t>(Color::Black)] = ((pawn & ~kFileA) >> 9) | ((pawn & ~kFileH) >> 7);
            }
            return table;
        }();

        constexpr auto kKnightAttacks = []() constexpr
        {
            std::array<Bitboard, 64> table{};
            // clang-format off
            constexpr std::array<std::array<int, 2>, 8> deltas{{
                { 1,  2},
                { 2,  1},
                { 2, -1},
                { 1, -2},
                {-1, -2},
                {-2, -1},
                {-2,  1},
                {-1,  2},
            }};
            // clang-format on
            for (std::size_t i = 0; i < 64; ++i)
            {
                Bitboard result = 0;
                const Square square = static_cast<Square>(i);
                const int file = file_of(square);
                const int rank = rank_of(square);
                for (const auto& d : deltas)
                {
                    const int df = d[0];
                    const int dr = d[1];
                    if (on_board(file + df, rank + dr))
                    {
                        result |= bit(to_square(file + df, rank + dr));
                    }
                }
                table[i] = result;
            }
            return table;
        }();

        constexpr auto kKingAttacks = []() constexpr
        {
            std::array<Bitboard, 64> table{};
            // clang-format off
            constexpr std::array<std::array<int, 2>, 8> deltas{{
                { 1,  1},
                { 1,  0},
                { 1, -1},
                { 0, -1},
                {-1, -1},
                {-1,  0},
                {-1,  1},
                { 0,  1},
            }};
            // clang-format on
            for (std::size_t i = 0; i < 64; ++i)
            {
                Bitboard result = 0;
                const Square square = static_cast<Square>(i);
                const int file = file_of(square);
                const int rank = rank_of(square);
                for (const auto& d : deltas)
                {
                    const int df = d[0];
                    const int dr = d[1];
                    if (on_board(file + df, rank + dr))
                    {
                        result |= bit(to_square(file + df, rank + dr));
                    }
                }
                table[i] = result;
            }
            return table;
        }();

    } // namespace

    Bitboard pawn_attacks(Square square, Color color) noexcept
    {
        return kPawnAttacks[static_cast<std::size_t>(square)][static_cast<std::size_t>(color)];
    }

    Bitboard knight_attacks(Square square) noexcept
    {
        return kKnightAttacks[static_cast<std::size_t>(square)];
    }

    Bitboard king_attacks(Square square) noexcept
    {
        return kKingAttacks[static_cast<std::size_t>(square)];
    }

    Bitboard bishop_attacks(Square square, Bitboard occupancy) noexcept
    {
        const auto index = static_cast<std::size_t>(square);
        return bishop_attack_tables[index][attack_table_index(occupancy, bishop_occupancy_masks[index])];
    }

    Bitboard rook_attacks(Square square, Bitboard occupancy) noexcept
    {
        const auto index = static_cast<std::size_t>(square);
        return rook_attack_tables[index][attack_table_index(occupancy, rook_occupancy_masks[index])];
    }

    Bitboard queen_attacks(Square square, Bitboard occupancy) noexcept
    {
        return bishop_attacks(square, occupancy) | rook_attacks(square, occupancy);
    }

    bool is_square_attacked(const Board& board, Square square, Color by)
    {
        const Bitboard target = bit(square);
        const Bitboard by_occupancy = board.occupancy(by);

        const Bitboard pawn_attackers = board.piece_bb(PieceType::Pawn) & by_occupancy;
        const Bitboard pawn_attacks = by == Color::White
                                          ? ((pawn_attackers & ~kFileA) << 7) | ((pawn_attackers & ~kFileH) << 9)
                                          : ((pawn_attackers & ~kFileA) >> 9) | ((pawn_attackers & ~kFileH) >> 7);
        if ((pawn_attacks & target) != 0)
        {
            return true;
        }

        Bitboard knights = board.piece_bb(PieceType::Knight) & by_occupancy;
        while (knights)
        {
            const auto from = static_cast<Square>(lsb_index(knights));
            if ((knight_attacks(from) & target) != 0)
            {
                return true;
            }
            knights &= knights - 1;
        }

        const Bitboard bishops_and_queens =
            (board.piece_bb(PieceType::Bishop) | board.piece_bb(PieceType::Queen)) & by_occupancy;
        if ((bishop_attacks(square, board.all_occupancy()) & bishops_and_queens) != 0)
        {
            return true;
        }

        const Bitboard rooks_and_queens =
            (board.piece_bb(PieceType::Rook) | board.piece_bb(PieceType::Queen)) & by_occupancy;
        if ((rook_attacks(square, board.all_occupancy()) & rooks_and_queens) != 0)
        {
            return true;
        }

        const Bitboard kings = board.piece_bb(PieceType::King) & by_occupancy;
        return kings != 0 && (king_attacks(static_cast<Square>(lsb_index(kings))) & target) != 0;
    }

    bool is_in_check(const Board& board, Color color)
    {
        const Bitboard king = board.piece_bb(PieceType::King) & board.occupancy(color);
        return king != 0 && is_square_attacked(board, static_cast<Square>(lsb_index(king)), ~color);
    }

} // namespace aurora::chess
