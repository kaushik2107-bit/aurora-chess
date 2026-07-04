#include "zobrist.hpp"

#include "bitboard.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace aurora::chess
{
    namespace
    {

        [[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t value) noexcept
        {
            value += 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }

        [[nodiscard]] constexpr std::size_t piece_index(Piece piece) noexcept
        {
            return static_cast<std::size_t>(piece) - 1;
        }

        constexpr auto kPieceSquareKeys = []() constexpr
        {
            std::array<std::array<Key, 64>, 12> keys{};
            std::uint64_t seed = 0x9d39247e33776d41ull;
            for (auto &piece_keys : keys)
            {
                for (auto &key : piece_keys)
                {
                    seed = splitmix64(seed);
                    key = seed;
                }
            }
            return keys;
        }();

        constexpr auto kCastlingKeys = []() constexpr
        {
            std::array<Key, 16> keys{};
            std::uint64_t seed = 0x2af7398005aaa5c7ull;
            for (auto &key : keys)
            {
                seed = splitmix64(seed);
                key = seed;
            }
            return keys;
        }();

        constexpr auto kEnPassantFileKeys = []() constexpr
        {
            std::array<Key, 8> keys{};
            std::uint64_t seed = 0x44db015024623547ull;
            for (auto &key : keys)
            {
                seed = splitmix64(seed);
                key = seed;
            }
            return keys;
        }();

        constexpr Key kBlackToMoveKey = splitmix64(0xf8d626aaaf278509ull);

    } // namespace

    Key zobrist::hash(const Board &board) noexcept
    {
        Key key = 0;
        for (std::size_t square_index = 0; square_index < 64; ++square_index)
        {
            const auto square = static_cast<Square>(square_index);
            const Piece piece = board.piece_on(square);
            if (piece != Piece::None)
            {
                key ^= kPieceSquareKeys[piece_index(piece)][square_index];
            }
        }

        if (board.side_to_move() == Color::Black)
        {
            key ^= kBlackToMoveKey;
        }

        key ^= kCastlingKeys[static_cast<std::size_t>(board.castling_rights()) & 0x0F];

        const Square ep = board.en_passant_square();
        if (ep != Square::NoSquare)
        {
            key ^= kEnPassantFileKeys[static_cast<std::size_t>(file_of(ep))];
        }

        return key;
    }

} // namespace aurora::chess
