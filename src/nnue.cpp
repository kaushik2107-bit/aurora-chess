#include "nnue.hpp"

#include "bitboard.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace aurora::chess
{
    namespace
    {
        constexpr std::uint32_t kStockfishVersion = 0x7AF32F20u;
        constexpr std::uint32_t kNetworkHash = 0x1C1020F2u;
        constexpr std::size_t kFeatureDimensions = 64 * 704 / 2;
        constexpr std::size_t kTransformerDimensions = 3072;
        constexpr std::size_t kPsqtBuckets = 8;
        constexpr std::size_t kLayerStacks = 8;
        constexpr std::size_t kFc0Outputs = 16;
        constexpr std::size_t kFc1Outputs = 32;
        constexpr int kWeightScaleBits = 6;
        constexpr int kOutputScale = 16;
        constexpr char kLebMagic[] = "COMPRESSED_LEB128";

        // Stockfish HalfKAv2_hm king buckets, expressed as feature offsets.
        constexpr std::array<std::array<int, 64>, 2> kKingBuckets{{
            {28,29,30,31,31,30,29,28,24,25,26,27,27,26,25,24,
             20,21,22,23,23,22,21,20,16,17,18,19,19,18,17,16,
             12,13,14,15,15,14,13,12,8,9,10,11,11,10,9,8,
             4,5,6,7,7,6,5,4,0,1,2,3,3,2,1,0},
            {0,1,2,3,3,2,1,0,4,5,6,7,7,6,5,4,
             8,9,10,11,11,10,9,8,12,13,14,15,15,14,13,12,
             16,17,18,19,19,18,17,16,20,21,22,23,23,22,21,20,
             24,25,26,27,27,26,25,24,28,29,30,31,31,30,29,28}
        }};

        template <typename T> bool read_value(std::istream& input, T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            input.read(reinterpret_cast<char*>(&value), sizeof(T));
            return !input.fail();
        }

        template <typename T> bool read_values(std::istream& input, T* values, std::size_t count)
        {
            input.read(reinterpret_cast<char*>(values), static_cast<std::streamsize>(sizeof(T) * count));
            return !input.fail();
        }

        template <typename T> bool read_leb128(std::istream& input, std::vector<T>& output)
        {
            std::array<char, sizeof(kLebMagic) - 1> magic{};
            input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            if (!input || !std::equal(magic.begin(), magic.end(), kLebMagic))
            {
                return false;
            }
            std::uint32_t bytes_left = 0;
            if (!read_value(input, bytes_left))
            {
                return false;
            }
            std::vector<std::uint8_t> compressed(bytes_left);
            if (!read_values(input, compressed.data(), compressed.size()))
            {
                return false;
            }
            std::size_t position = 0;
            for (T& destination : output)
            {
                using U = std::make_unsigned_t<T>;
                U result = 0;
                unsigned shift = 0;
                std::uint8_t byte = 0;
                do
                {
                    if (position >= compressed.size() || shift >= sizeof(T) * 8)
                    {
                        return false;
                    }
                    byte = compressed[position++];
                    result |= static_cast<U>(byte & 0x7f) << shift;
                    shift += 7;
                } while ((byte & 0x80) != 0);
                if (shift < sizeof(T) * 8 && (byte & 0x40) != 0)
                {
                    result |= static_cast<U>(~U{0}) << shift;
                }
                destination = static_cast<T>(result);
            }
            return position == compressed.size();
        }

        [[nodiscard]] std::size_t feature_index(Color perspective, Piece piece, Square square,
                                                Square king_square) noexcept
        {
            const auto p = static_cast<std::size_t>(perspective);
            const int king = static_cast<int>(king_square);
            const int orientation = perspective == Color::White ? (king % 8 < 4 ? 7 : 0)
                                                                 : (king % 8 < 4 ? 63 : 56);
            const PieceType type = piece_type(piece);
            const int slot = type == PieceType::King
                                 ? 10
                                 : 2 * static_cast<int>(type) + (piece_color(piece) != perspective ? 1 : 0);
            return static_cast<std::size_t>(kKingBuckets[p][king] * 704 + slot * 64 +
                                            (static_cast<int>(square) ^ orientation));
        }

        [[nodiscard]] std::vector<std::string> default_network_paths()
        {
            std::vector<std::string> paths;
            if (const char* path = std::getenv("AURORA_NNUE_PATH"))
            {
                paths.emplace_back(path);
            }
            paths.emplace_back("data/nn-1c0000000000.nnue");
            paths.emplace_back("../data/nn-1c0000000000.nnue");
            paths.emplace_back("../../data/nn-1c0000000000.nnue");
            paths.emplace_back("../../../data/nn-1c0000000000.nnue");
            return paths;
        }

        [[nodiscard]] std::uint8_t clipped_relu(std::int32_t value) noexcept
        {
            return static_cast<std::uint8_t>(std::clamp(value >> kWeightScaleBits, 0, 127));
        }
    }

    struct NnueEvaluator::Network
    {
        struct Stack
        {
            std::array<std::int32_t, kFc0Outputs> fc0_bias{};
            // Input-major layout keeps all 16 weights needed for a non-zero
            // transformed feature contiguous during sparse inference.
            std::array<std::int8_t, kTransformerDimensions * kFc0Outputs> fc0_weight{};
            std::array<std::int32_t, kFc1Outputs> fc1_bias{};
            std::array<std::int8_t, kFc1Outputs * 32> fc1_weight{};
            std::int32_t output_bias{0};
            std::array<std::int8_t, 32> output_weight{};
        };

        std::array<std::int16_t, kTransformerDimensions> bias{};
        std::vector<std::int16_t> weights{std::vector<std::int16_t>(kFeatureDimensions * kTransformerDimensions)};
        std::vector<std::int32_t> psqt{std::vector<std::int32_t>(kFeatureDimensions * kPsqtBuckets)};
        std::array<Stack, kLayerStacks> stacks{};
        bool loaded{false};
    };

    NnueEvaluator::NnueEvaluator() : network_(std::make_unique<Network>()) {}
    NnueEvaluator::~NnueEvaluator() = default;
    NnueEvaluator::NnueEvaluator(NnueEvaluator&&) noexcept = default;
    NnueEvaluator& NnueEvaluator::operator=(NnueEvaluator&&) noexcept = default;

    bool NnueEvaluator::load(std::string_view path)
    {
        std::ifstream input{std::string(path), std::ios::binary};
        if (!input)
        {
            return false;
        }
        std::uint32_t version = 0, hash = 0, description_size = 0;
        if (!read_value(input, version) || !read_value(input, hash) || !read_value(input, description_size) ||
            version != kStockfishVersion || hash != kNetworkHash || description_size > 4096)
        {
            return false;
        }
        input.ignore(description_size);
        auto next = std::make_unique<Network>();
        std::vector<std::int16_t> bias(kTransformerDimensions);
        std::uint32_t component_hash = 0;
        if (!read_value(input, component_hash) || component_hash != (0x7F234CB8u ^ 6144u) ||
            !read_leb128(input, bias) || !read_leb128(input, next->weights) || !read_leb128(input, next->psqt))
        {
            return false;
        }
        for (std::size_t i = 0; i < kTransformerDimensions; ++i)
        {
            next->bias[i] = static_cast<std::int16_t>(bias[i] * 2);
        }
        for (auto& weight : next->weights)
        {
            weight = static_cast<std::int16_t>(weight * 2);
        }
        for (auto& stack : next->stacks)
        {
            std::array<std::int8_t, kFc0Outputs * kTransformerDimensions> file_fc0_weight{};
            if (!read_value(input, component_hash) ||
                !read_values(input, stack.fc0_bias.data(), stack.fc0_bias.size()) ||
                !read_values(input, file_fc0_weight.data(), file_fc0_weight.size()) ||
                !read_values(input, stack.fc1_bias.data(), stack.fc1_bias.size()) ||
                !read_values(input, stack.fc1_weight.data(), stack.fc1_weight.size()) ||
                !read_value(input, stack.output_bias) ||
                !read_values(input, stack.output_weight.data(), stack.output_weight.size()))
            {
                return false;
            }
            for (std::size_t out = 0; out < kFc0Outputs; ++out)
            {
                for (std::size_t in = 0; in < kTransformerDimensions; ++in)
                {
                    stack.fc0_weight[(in / 4) * (kFc0Outputs * 4) + out * 4 + in % 4] =
                        file_fc0_weight[out * kTransformerDimensions + in];
                }
            }
        }
        if (input.peek() != std::char_traits<char>::eof())
        {
            return false;
        }
        next->loaded = true;
        network_ = std::move(next);
        path_ = std::string(path);
        return true;
    }

    bool NnueEvaluator::load_default()
    {
        for (const auto& path : default_network_paths())
        {
            if (load(path))
            {
                return true;
            }
        }
        return false;
    }

    bool NnueEvaluator::is_loaded() const noexcept { return network_ && network_->loaded; }
    std::string_view NnueEvaluator::path() const noexcept { return path_; }

    void NnueEvaluator::refresh_perspective(const Board& board, Color perspective, Accumulator& accumulator,
                                            RefreshCache* cache) const noexcept
    {
        const auto p = static_cast<std::size_t>(perspective);
        const Square king = board.king_square(perspective);
        if (cache == nullptr)
        {
            std::copy(network_->bias.begin(), network_->bias.end(), accumulator.values[p].begin());
            accumulator.psqt[p].fill(0);
            Bitboard pieces = board.all_occupancy();
            while (pieces)
            {
                const Square square = static_cast<Square>(lsb_index(pieces));
                const std::size_t index = feature_index(perspective, board.piece_on(square), square, king);
                const auto* weight = network_->weights.data() + index * kTransformerDimensions;
                for (std::size_t i = 0; i < kTransformerDimensions; ++i)
                {
                    accumulator.values[p][i] += weight[i];
                }
                const auto* psqt = network_->psqt.data() + index * kPsqtBuckets;
                for (std::size_t i = 0; i < kPsqtBuckets; ++i)
                {
                    accumulator.psqt[p][i] += psqt[i];
                }
                pieces &= pieces - 1;
            }
            return;
        }

        const std::size_t bucket = static_cast<std::size_t>(kKingBuckets[p][static_cast<int>(king)]);
        auto& entry = cache->entries[p][bucket];
        std::array<std::uint64_t, 11> desired{};
        Bitboard pieces = board.all_occupancy();
        while (pieces)
        {
            const Square square = static_cast<Square>(lsb_index(pieces));
            const std::size_t local = feature_index(perspective, board.piece_on(square), square, king) % 704;
            desired[local / 64] |= std::uint64_t{1} << (local % 64);
            pieces &= pieces - 1;
        }
        if (!entry.valid)
        {
            std::copy(network_->bias.begin(), network_->bias.end(), entry.values.begin());
            entry.psqt.fill(0);
        }
        for (std::size_t word = 0; word < desired.size(); ++word)
        {
            std::uint64_t changed = desired[word] ^ entry.features[word];
            while (changed)
            {
                const std::size_t bit = static_cast<std::size_t>(std::countr_zero(changed));
                const std::size_t local = word * 64 + bit;
                const int sign = (desired[word] & (std::uint64_t{1} << bit)) != 0 ? 1 : -1;
                const std::size_t index = bucket * 704 + local;
                const auto* weight = network_->weights.data() + index * kTransformerDimensions;
                for (std::size_t i = 0; i < kTransformerDimensions; ++i)
                {
                    entry.values[i] += sign * weight[i];
                }
                const auto* psqt = network_->psqt.data() + index * kPsqtBuckets;
                for (std::size_t i = 0; i < kPsqtBuckets; ++i)
                {
                    entry.psqt[i] += sign * psqt[i];
                }
                changed &= changed - 1;
            }
        }
        entry.features = desired;
        entry.valid = true;
        accumulator.values[p] = entry.values;
        accumulator.psqt[p] = entry.psqt;
    }

    void NnueEvaluator::refresh_accumulator(const Board& board, Accumulator& accumulator,
                                            RefreshCache* cache) const noexcept
    {
        if (!is_loaded() || board.king_square(Color::White) == Square::NoSquare ||
            board.king_square(Color::Black) == Square::NoSquare)
        {
            accumulator.valid = false;
            return;
        }
        for (Color perspective : {Color::White, Color::Black})
        {
            refresh_perspective(board, perspective, accumulator, cache);
        }
        accumulator.king_squares = {board.king_square(Color::White), board.king_square(Color::Black)};
        accumulator.valid = true;
    }

    void NnueEvaluator::update_accumulator(const Accumulator& previous, const Board& board,
                                           const Board::DirtyPiece& dirty,
                                           Accumulator& next, RefreshCache* cache) const noexcept
    {
        next.psqt = previous.psqt;
        for (Color perspective : {Color::White, Color::Black})
        {
            const auto p = static_cast<std::size_t>(perspective);
            bool refresh = false;
            for (std::size_t i = 0; i < dirty.removed_count; ++i)
            {
                refresh |= piece_type(dirty.removed_pieces[i]) == PieceType::King &&
                           piece_color(dirty.removed_pieces[i]) == perspective;
            }
            if (refresh)
            {
                refresh_perspective(board, perspective, next, cache);
                continue;
            }
            const Square king = previous.king_squares[p];
            std::array<const std::int16_t*, Board::kMaxDirtyPieces> removed{};
            std::array<const std::int16_t*, Board::kMaxDirtyPieces> added{};
            auto apply = [&](Piece piece, Square square, int sign)
            {
                const std::size_t index = feature_index(perspective, piece, square, king);
                const auto* psqt = network_->psqt.data() + index * kPsqtBuckets;
                for (std::size_t j = 0; j < kPsqtBuckets; ++j)
                {
                    next.psqt[p][j] += sign * psqt[j];
                }
            };
            for (std::size_t i = 0; i < dirty.removed_count; ++i)
            {
                const std::size_t index = feature_index(perspective, dirty.removed_pieces[i],
                                                        dirty.removed_squares[i], king);
                removed[i] = network_->weights.data() + index * kTransformerDimensions;
                apply(dirty.removed_pieces[i], dirty.removed_squares[i], -1);
            }
            for (std::size_t i = 0; i < dirty.added_count; ++i)
            {
                const std::size_t index = feature_index(perspective, dirty.added_pieces[i],
                                                        dirty.added_squares[i], king);
                added[i] = network_->weights.data() + index * kTransformerDimensions;
                apply(dirty.added_pieces[i], dirty.added_squares[i], 1);
            }
            if (dirty.removed_count == 1 && dirty.added_count == 1)
            {
                for (std::size_t j = 0; j < kTransformerDimensions; ++j)
                {
                    next.values[p][j] = static_cast<std::int16_t>(previous.values[p][j] - removed[0][j] + added[0][j]);
                }
            }
            else if (dirty.removed_count == 2 && dirty.added_count == 1)
            {
                for (std::size_t j = 0; j < kTransformerDimensions; ++j)
                {
                    next.values[p][j] = static_cast<std::int16_t>(previous.values[p][j] - removed[0][j] -
                                                                 removed[1][j] + added[0][j]);
                }
            }
            else if (dirty.removed_count == 2 && dirty.added_count == 2)
            {
                for (std::size_t j = 0; j < kTransformerDimensions; ++j)
                {
                    next.values[p][j] = static_cast<std::int16_t>(previous.values[p][j] - removed[0][j] -
                                                                 removed[1][j] + added[0][j] + added[1][j]);
                }
            }
            else
            {
                next.values[p] = previous.values[p];
                for (std::size_t i = 0; i < dirty.removed_count; ++i)
                {
                    for (std::size_t j = 0; j < kTransformerDimensions; ++j)
                    {
                        next.values[p][j] -= removed[i][j];
                    }
                }
                for (std::size_t i = 0; i < dirty.added_count; ++i)
                {
                    for (std::size_t j = 0; j < kTransformerDimensions; ++j)
                    {
                        next.values[p][j] += added[i][j];
                    }
                }
            }
        }
        next.king_squares = {board.king_square(Color::White), board.king_square(Color::Black)};
        next.valid = true;
    }

    Score NnueEvaluator::evaluate(const Board& board) const noexcept
    {
        Accumulator accumulator;
        refresh_accumulator(board, accumulator);
        return evaluate(board, accumulator);
    }

    Score NnueEvaluator::evaluate(const Board& board, const Accumulator& accumulator) const noexcept
    {
        if (!is_loaded() || !accumulator.valid)
        {
            return fallback_.evaluate(board);
        }
        const std::size_t stm = static_cast<std::size_t>(board.side_to_move());
        const std::size_t other = 1 - stm;
        std::array<std::uint8_t, kTransformerDimensions> transformed{};
        constexpr std::size_t half = kTransformerDimensions / 2;
        for (std::size_t p = 0; p < 2; ++p)
        {
            const auto& values = accumulator.values[p == 0 ? stm : other];
            const std::size_t offset = p * half;
            for (std::size_t i = 0; i < half; ++i)
            {
                const int a = std::clamp<int>(values[i], 0, 254);
                const int b = std::clamp<int>(values[i + half], 0, 254);
                transformed[offset + i] = static_cast<std::uint8_t>(a * b / 512);
            }
        }
        const int piece_count = popcount(board.all_occupancy());
        const std::size_t bucket = static_cast<std::size_t>(std::clamp((piece_count - 1) / 4, 0, 7));
        const auto& stack = network_->stacks[bucket];
        std::array<std::int32_t, kFc0Outputs> fc0{};
#if defined(__AVXVNNI__)
        __m256i low = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(stack.fc0_bias.data()));
        __m256i high = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(stack.fc0_bias.data() + 8));
        for (std::size_t in = 0; in < kTransformerDimensions; in += 4)
        {
            const std::uint32_t inputs = static_cast<std::uint32_t>(transformed[in]) |
                                         (static_cast<std::uint32_t>(transformed[in + 1]) << 8) |
                                         (static_cast<std::uint32_t>(transformed[in + 2]) << 16) |
                                         (static_cast<std::uint32_t>(transformed[in + 3]) << 24);
            if (inputs == 0)
            {
                continue;
            }
            const __m256i packed = _mm256_set1_epi32(static_cast<int>(inputs));
            const auto* weights = stack.fc0_weight.data() + (in / 4) * (kFc0Outputs * 4);
            low = _mm256_dpbusd_avx_epi32(low, packed,
                                          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights)));
            high = _mm256_dpbusd_avx_epi32(high, packed,
                                           _mm256_loadu_si256(reinterpret_cast<const __m256i*>(weights + 32)));
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(fc0.data()), low);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(fc0.data() + 8), high);
#else
        fc0 = stack.fc0_bias;
        for (std::size_t in = 0; in < kTransformerDimensions; ++in)
        {
            const int value = transformed[in];
            if (value == 0)
            {
                continue;
            }
            for (std::size_t out = 0; out < kFc0Outputs; ++out)
            {
                fc0[out] += stack.fc0_weight[(in / 4) * (kFc0Outputs * 4) + out * 4 + in % 4] * value;
            }
        }
#endif
        std::array<std::uint8_t, 32> hidden_input{};
        for (std::size_t i = 0; i < 15; ++i)
        {
            hidden_input[i] = static_cast<std::uint8_t>(std::min<std::int64_t>(127,
                static_cast<std::int64_t>(fc0[i]) * fc0[i] >> 19));
            hidden_input[15 + i] = clipped_relu(fc0[i]);
        }
        std::array<std::int32_t, 32> fc1 = stack.fc1_bias;
        for (std::size_t out = 0; out < 32; ++out)
        {
            for (std::size_t in = 0; in < 32; ++in)
            {
                fc1[out] += stack.fc1_weight[out * 32 + in] * hidden_input[in];
            }
        }
        std::int32_t positional = stack.output_bias;
        for (std::size_t i = 0; i < 32; ++i)
        {
            positional += stack.output_weight[i] * clipped_relu(fc1[i]);
        }
        positional += fc0[15] * (600 * kOutputScale) / (127 * (1 << kWeightScaleBits));
        positional /= kOutputScale;
        const std::int32_t psqt = (accumulator.psqt[stm][bucket] - accumulator.psqt[other][bucket]) /
                                  (2 * kOutputScale);
        std::int32_t score = (125 * psqt + 131 * positional) / 128;
        const int complexity = std::abs(psqt - positional);
        score -= score * complexity / 18000;
        score -= score * static_cast<int>(board.halfmove_clock()) / 212;
        int material = 0;
        constexpr std::array<int, 6> material_weights{1, 3, 3, 5, 9, 0};
        for (std::size_t type = 0; type < 5; ++type)
        {
            material += material_weights[type] * popcount(board.piece_bb(static_cast<PieceType>(type)));
        }
        const double m = std::clamp(material, 17, 78) / 58.0;
        constexpr std::array<double, 4> coefficients{-13.50030198, 40.92780883, -36.82753545, 386.83004070};
        const double normalization = ((coefficients[0] * m + coefficients[1]) * m + coefficients[2]) * m +
                                     coefficients[3];
        const Score centipawns = static_cast<Score>(std::lround(100.0 * score / normalization));
        return std::clamp<Score>(centipawns, -29'000, 29'000);
    }

} // namespace aurora::chess
