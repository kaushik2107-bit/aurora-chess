#include "nnue.hpp"

#include "bitboard.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(_MSC_VER)
#define AURORA_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define AURORA_FORCE_INLINE inline __attribute__((always_inline))
#else
#define AURORA_FORCE_INLINE inline
#endif

namespace aurora::chess
{
    namespace
    {

        constexpr std::size_t kAccumulatorSize = NnueEvaluator::kAccumulatorSize;
        constexpr std::size_t kLayer1InputSize = 2 * kAccumulatorSize;
        constexpr std::size_t kLayer2Size = 32;
        constexpr std::size_t kLayer3Size = 32;
        constexpr std::size_t kKingBuckets = 32;
        constexpr std::size_t kFeaturePieceSlots = 10;
        constexpr std::size_t kFeatureCount = kKingBuckets * kFeaturePieceSlots * 64;
        constexpr std::int32_t kAccumulatorScale = INT16_MAX / 3;
        constexpr std::int32_t kActivationScale = 1024;
        constexpr std::int32_t kDenseWeightScale = 1024;
        constexpr std::int32_t kDenseScale = kActivationScale * kDenseWeightScale;
        constexpr std::size_t kSimdAlignment = 32;
        constexpr int kBlackPerspectiveXor = 56;

        static_assert(kLayer1InputSize % 16 == 0);
        static_assert(kLayer2Size % 16 == 0);
        static_assert(kLayer3Size % 16 == 0);

        template <typename T, std::size_t Alignment> class AlignedAllocator
        {
        public:
            using value_type = T;

            AlignedAllocator() noexcept = default;

            template <typename U> constexpr AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

            [[nodiscard]] T* allocate(std::size_t count)
            {
                if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
                {
                    throw std::bad_array_new_length{};
                }

                return static_cast<T*>(::operator new(count * sizeof(T), std::align_val_t{Alignment}));
            }

            void deallocate(T* pointer, std::size_t) noexcept
            {
                ::operator delete(pointer, std::align_val_t{Alignment});
            }

            template <typename U> struct rebind
            {
                using other = AlignedAllocator<U, Alignment>;
            };
        };

        template <typename T, typename U, std::size_t Alignment>
        [[nodiscard]] constexpr bool operator==(const AlignedAllocator<T, Alignment>&,
                                                const AlignedAllocator<U, Alignment>&) noexcept
        {
            return true;
        }

        template <typename T, typename U, std::size_t Alignment>
        [[nodiscard]] constexpr bool operator!=(const AlignedAllocator<T, Alignment>& lhs,
                                                const AlignedAllocator<U, Alignment>& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        template <typename T> using AlignedVector = std::vector<T, AlignedAllocator<T, kSimdAlignment>>;

        [[nodiscard]] bool is_aligned(const void* pointer) noexcept
        {
            return reinterpret_cast<std::uintptr_t>(pointer) % kSimdAlignment == 0;
        }

        // clang-format off
        constexpr std::array<std::uint8_t, 64> kKingBucket{
             0,  1,  2,  3,  4,  5,  6,  7,
             8,  9, 10, 11, 12, 13, 14, 15,
            16, 16, 17, 17, 18, 18, 19, 19,
            20, 20, 21, 21, 22, 22, 23, 23,
            24, 24, 25, 25, 26, 26, 27, 27,
            24, 24, 25, 25, 26, 26, 27, 27,
            28, 28, 29, 29, 30, 30, 31, 31,
            28, 28, 29, 29, 30, 30, 31, 31,
        };
        // clang-format on

        [[nodiscard]] constexpr std::size_t color_index(Color color) noexcept
        {
            return static_cast<std::size_t>(color);
        }

        [[nodiscard]] constexpr bool is_black_perspective(Color perspective) noexcept
        {
            return perspective == Color::Black;
        }

        [[nodiscard]] std::size_t feature_index(Color perspective, Color piece_color, PieceType piece_type,
                                                Square square, Square king_square) noexcept
        {
            const int perspective_xor = is_black_perspective(perspective) ? kBlackPerspectiveXor : 0;
            const auto king = static_cast<std::size_t>(static_cast<int>(king_square) ^ perspective_xor);
            const auto transformed_square = static_cast<std::size_t>(static_cast<int>(square) ^ perspective_xor);
            const auto piece_slot =
                static_cast<std::size_t>(piece_type) * 2 +
                static_cast<std::size_t>(is_black_perspective(perspective) ^ (piece_color == Color::Black));

            return static_cast<std::size_t>(kKingBucket[king]) * kFeaturePieceSlots * 64 + piece_slot * 64 +
                   transformed_square;
        }

        template <std::size_t Size> [[nodiscard]] std::array<float, Size> read_float_array(std::istream& input)
        {
            std::array<float, Size> values{};
            input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(sizeof(float) * Size));
            return values;
        }

        [[nodiscard]] bool read_floats(std::istream& input, std::vector<float>& values)
        {
            input.read(reinterpret_cast<char*>(values.data()),
                       static_cast<std::streamsize>(sizeof(float) * values.size()));
            return !input.fail();
        }

        [[nodiscard]] std::int32_t to_fixed(float value) noexcept
        {
            return static_cast<std::int32_t>(std::lround(value * static_cast<float>(kAccumulatorScale)));
        }

        [[nodiscard]] std::int16_t to_dense_weight(float value) noexcept
        {
            const auto scaled = static_cast<std::int32_t>(std::lround(value * static_cast<float>(kDenseWeightScale)));
            return static_cast<std::int16_t>(
                std::clamp(scaled, static_cast<std::int32_t>(INT16_MIN), static_cast<std::int32_t>(INT16_MAX)));
        }

        [[nodiscard]] std::int32_t to_dense_bias(float value) noexcept
        {
            return static_cast<std::int32_t>(std::lround(value * static_cast<float>(kDenseScale)));
        }

        [[nodiscard]] std::int16_t to_activation(std::int32_t value) noexcept
        {
            const auto clipped = std::clamp(value, std::int32_t{0}, kAccumulatorScale);
            const auto scaled =
                (static_cast<std::int64_t>(clipped) * kActivationScale + kAccumulatorScale / 2) / kAccumulatorScale;
            return static_cast<std::int16_t>(scaled);
        }

        [[nodiscard]] std::int16_t to_clipped_activation(std::int32_t value) noexcept
        {
            const auto clipped = std::clamp(value, std::int32_t{0}, kDenseScale);
            return static_cast<std::int16_t>((clipped + kDenseWeightScale / 2) / kDenseWeightScale);
        }

        [[nodiscard]] Score to_score(std::int32_t value) noexcept
        {
            const auto scaled = static_cast<std::int64_t>(value) * 100;
            const auto rounded = scaled >= 0 ? scaled + kDenseScale / 2 : scaled - kDenseScale / 2;
            return static_cast<Score>(rounded / kDenseScale);
        }

        void quantize_weights(const std::vector<float>& source, AlignedVector<std::int16_t>& destination) noexcept
        {
            for (std::size_t i = 0; i < source.size(); ++i)
            {
                destination[i] = to_dense_weight(source[i]);
            }
        }

        template <std::size_t Size>
        [[nodiscard]] std::array<std::int32_t, Size> quantize_biases(const std::array<float, Size>& source) noexcept
        {
            std::array<std::int32_t, Size> destination{};
            for (std::size_t i = 0; i < source.size(); ++i)
            {
                destination[i] = to_dense_bias(source[i]);
            }
            return destination;
        }

#if defined(__AVX2__)
        [[nodiscard]] AURORA_FORCE_INLINE std::int32_t horizontal_sum_i32(__m256i value) noexcept
        {
            const __m128i high = _mm256_extracti128_si256(value, 1);
            const __m128i low = _mm256_castsi256_si128(value);
            __m128i sum = _mm_add_epi32(low, high);
            sum = _mm_hadd_epi32(sum, sum);
            sum = _mm_hadd_epi32(sum, sum);
            return _mm_cvtsi128_si32(sum);
        }
#endif

        [[nodiscard]] AURORA_FORCE_INLINE std::int32_t dot_product_i16(const std::int16_t* lhs, const std::int16_t* rhs,
                                                                       std::size_t count) noexcept
        {
            std::size_t index = 0;
            std::int32_t sum = 0;

#if defined(__AVX2__)
            assert(is_aligned(lhs));
            assert(is_aligned(rhs));
            __m256i packed_sum = _mm256_setzero_si256();
            for (; index + 16 <= count; index += 16)
            {
                const __m256i lhs_values = _mm256_load_si256(reinterpret_cast<const __m256i*>(lhs + index));
                const __m256i rhs_values = _mm256_load_si256(reinterpret_cast<const __m256i*>(rhs + index));
                packed_sum = _mm256_add_epi32(packed_sum, _mm256_madd_epi16(lhs_values, rhs_values));
            }
            sum = horizontal_sum_i32(packed_sum);
#endif

            for (; index < count; ++index)
            {
                sum += lhs[index] * rhs[index];
            }

            return sum;
        }

        template <std::size_t In, std::size_t Out>
        void dense_clipped_relu(const std::array<std::int16_t, In>& input, const AlignedVector<std::int16_t>& weights,
                                const std::array<std::int32_t, Out>& biases,
                                std::array<std::int16_t, Out>& output) noexcept
        {
            const std::int16_t* input_data = input.data();
            const std::int16_t* weights_data = weights.data();
            for (std::size_t row = 0; row < Out; ++row)
            {
                const std::int16_t* row_weights = weights_data + row * In;
                const std::int32_t value = biases[row] + dot_product_i16(input_data, row_weights, In);
                output[row] = to_clipped_activation(value);
            }
        }

        template <std::size_t In, std::size_t Out>
        void dense_linear(const std::array<std::int16_t, In>& input, const AlignedVector<std::int16_t>& weights,
                          const std::array<std::int32_t, Out>& biases, std::array<std::int32_t, Out>& output) noexcept
        {
            const std::int16_t* input_data = input.data();
            const std::int16_t* weights_data = weights.data();
            for (std::size_t row = 0; row < Out; ++row)
            {
                const std::int16_t* row_weights = weights_data + row * In;
                output[row] = biases[row] + dot_product_i16(input_data, row_weights, In);
            }
        }

        [[nodiscard]] std::vector<std::string> default_network_paths()
        {
            std::vector<std::string> paths;
            if (const char* env_path = std::getenv("AURORA_NNUE_PATH"))
            {
                paths.emplace_back(env_path);
            }

            paths.emplace_back("data/network-20220625.nnue");
            paths.emplace_back("../data/network-20220625.nnue");
            paths.emplace_back("../../data/network-20220625.nnue");
            paths.emplace_back("../../../data/network-20220625.nnue");
            return paths;
        }

    } // namespace

    struct NnueEvaluator::Network
    {
        AlignedVector<std::int16_t> feature_weights;
        std::array<std::int32_t, kAccumulatorSize> feature_biases{};
        AlignedVector<std::int16_t> fc1_weights;
        std::array<std::int32_t, kLayer2Size> fc1_biases{};
        AlignedVector<std::int16_t> fc2_weights;
        std::array<std::int32_t, kLayer3Size> fc2_biases{};
        AlignedVector<std::int16_t> output_weights;
        std::array<std::int32_t, 1> output_bias{};
        bool loaded{false};

        Network()
            : feature_weights(kFeatureCount * kAccumulatorSize), fc1_weights(kLayer1InputSize * kLayer2Size),
              fc2_weights(kLayer2Size * kLayer3Size), output_weights(kLayer3Size)
        {
        }
    };

    NnueEvaluator::NnueEvaluator() : network_(std::make_unique<Network>()) {}

    NnueEvaluator::~NnueEvaluator() = default;

    NnueEvaluator::NnueEvaluator(NnueEvaluator&&) noexcept = default;

    NnueEvaluator& NnueEvaluator::operator=(NnueEvaluator&&) noexcept = default;

    bool NnueEvaluator::load(std::string_view path)
    {
        auto next = std::make_unique<Network>();
        std::ifstream input{std::string{path}, std::ios::binary};
        if (!input.is_open())
        {
            return false;
        }

        input.read(reinterpret_cast<char*>(next->feature_weights.data()),
                   static_cast<std::streamsize>(sizeof(std::int16_t) * next->feature_weights.size()));
        if (input.fail())
        {
            return false;
        }

        const auto feature_biases = read_float_array<kAccumulatorSize>(input);
        for (std::size_t i = 0; i < feature_biases.size(); ++i)
        {
            next->feature_biases[i] = to_fixed(feature_biases[i]);
        }
        std::vector<float> fc1_weights(next->fc1_weights.size());
        if (!read_floats(input, fc1_weights))
        {
            return false;
        }
        quantize_weights(fc1_weights, next->fc1_weights);
        next->fc1_biases = quantize_biases(read_float_array<kLayer2Size>(input));

        std::vector<float> fc2_weights(next->fc2_weights.size());
        if (!read_floats(input, fc2_weights))
        {
            return false;
        }
        quantize_weights(fc2_weights, next->fc2_weights);
        next->fc2_biases = quantize_biases(read_float_array<kLayer3Size>(input));

        std::vector<float> output_weights(next->output_weights.size());
        if (!read_floats(input, output_weights))
        {
            return false;
        }
        quantize_weights(output_weights, next->output_weights);
        next->output_bias = quantize_biases(read_float_array<1>(input));

        if (input.fail())
        {
            return false;
        }

        next->loaded = true;
        network_ = std::move(next);
        path_ = std::string{path};
        return true;
    }

    bool NnueEvaluator::load_default()
    {
        for (const auto& candidate : default_network_paths())
        {
            if (load(candidate))
            {
                return true;
            }
        }

        return false;
    }

    bool NnueEvaluator::is_loaded() const noexcept
    {
        return network_ != nullptr && network_->loaded;
    }

    std::string_view NnueEvaluator::path() const noexcept
    {
        return path_;
    }

    void NnueEvaluator::apply_feature(Accumulator& accumulator, Color perspective, Color piece_color,
                                      PieceType piece_type, Square square, int sign) const noexcept
    {
        if (!is_loaded() || piece_type == PieceType::King || piece_type == PieceType::Count)
        {
            return;
        }

        const auto index = feature_index(perspective, piece_color, piece_type, square,
                                         accumulator.king_squares[color_index(perspective)]);
        const auto* weights = network_->feature_weights.data() + index * kAccumulatorSize;
        auto& view = accumulator.values[color_index(perspective)];
        for (std::size_t i = 0; i < kAccumulatorSize; ++i)
        {
            view[i] += sign * weights[i];
        }
    }

    Score NnueEvaluator::evaluate(const Board& board) const noexcept
    {
        if (!is_loaded() || board.king_square(Color::White) == Square::NoSquare ||
            board.king_square(Color::Black) == Square::NoSquare)
        {
            return fallback_.evaluate(board);
        }

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

        const Color side = board.side_to_move();
        const Color other = ~side;
        alignas(kSimdAlignment) std::array<std::int16_t, kLayer1InputSize> transformed{};
        for (std::size_t i = 0; i < kAccumulatorSize; ++i)
        {
            transformed[i] = to_activation(accumulator.values[color_index(side)][i]);
            transformed[i + kAccumulatorSize] = to_activation(accumulator.values[color_index(other)][i]);
        }

        alignas(kSimdAlignment) std::array<std::int16_t, kLayer2Size> layer1{};
        alignas(kSimdAlignment) std::array<std::int16_t, kLayer3Size> layer2{};
        std::array<std::int32_t, 1> output{};
        dense_clipped_relu(transformed, network_->fc1_weights, network_->fc1_biases, layer1);
        dense_clipped_relu(layer1, network_->fc2_weights, network_->fc2_biases, layer2);
        dense_linear(layer2, network_->output_weights, network_->output_bias, output);
        return to_score(output[0]);
    }

    void NnueEvaluator::refresh_accumulator(const Board& board, Accumulator& accumulator) const noexcept
    {
        if (!is_loaded() || board.king_square(Color::White) == Square::NoSquare ||
            board.king_square(Color::Black) == Square::NoSquare)
        {
            accumulator.valid = false;
            return;
        }

        accumulator.values = {
            network_->feature_biases,
            network_->feature_biases,
        };
        accumulator.king_squares = {
            board.king_square(Color::White),
            board.king_square(Color::Black),
        };
        accumulator.valid = true;

        for (Color piece_color : {Color::White, Color::Black})
        {
            const Bitboard own = board.occupancy(piece_color);
            for (PieceType type :
                 {PieceType::Pawn, PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen})
            {
                Bitboard pieces = board.piece_bb(type) & own;
                while (pieces != 0)
                {
                    const auto square = static_cast<Square>(lsb_index(pieces));
                    for (Color perspective : {Color::White, Color::Black})
                    {
                        apply_feature(accumulator, perspective, piece_color, type, square, 1);
                    }
                    pieces &= pieces - 1;
                }
            }
        }
    }

    void NnueEvaluator::update_accumulator(const Accumulator& previous, const Board& board,
                                           const Board::DirtyPiece& dirty, Accumulator& next) const noexcept
    {
        if (!is_loaded() || !previous.valid ||
            previous.king_squares[color_index(Color::White)] != board.king_square(Color::White) ||
            previous.king_squares[color_index(Color::Black)] != board.king_square(Color::Black))
        {
            refresh_accumulator(board, next);
            return;
        }

        next = previous;

        for (std::uint8_t i = 0; i < dirty.removed_count; ++i)
        {
            const Piece piece = dirty.removed_pieces[i];
            if (piece_type(piece) == PieceType::King)
            {
                refresh_accumulator(board, next);
                return;
            }
            for (Color perspective : {Color::White, Color::Black})
            {
                apply_feature(next, perspective, piece_color(piece), piece_type(piece), dirty.removed_squares[i], -1);
            }
        }

        for (std::uint8_t i = 0; i < dirty.added_count; ++i)
        {
            const Piece piece = dirty.added_pieces[i];
            if (piece_type(piece) == PieceType::King)
            {
                refresh_accumulator(board, next);
                return;
            }
            for (Color perspective : {Color::White, Color::Black})
            {
                apply_feature(next, perspective, piece_color(piece), piece_type(piece), dirty.added_squares[i], 1);
            }
        }

        next.valid = true;
    }

} // namespace aurora::chess

#undef AURORA_FORCE_INLINE
