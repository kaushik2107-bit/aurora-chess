#include "nnue.hpp"

#include "bitboard.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

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
        constexpr float kConversionFactor = static_cast<float>(INT16_MAX / 3);
        constexpr int kBlackPerspectiveXor = 56;

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

        template <std::size_t In, std::size_t Out>
        void dense_clipped_relu(const std::array<float, In>& input, const std::vector<float>& weights,
                                const std::array<float, Out>& biases, std::array<float, Out>& output) noexcept
        {
            for (std::size_t row = 0; row < Out; ++row)
            {
                const auto begin = weights.begin() + static_cast<std::ptrdiff_t>(row * In);
                float value = biases[row] + std::inner_product(input.begin(), input.end(), begin, 0.0f);
                output[row] = std::clamp(value, 0.0f, 1.0f);
            }
        }

        template <std::size_t In, std::size_t Out>
        void dense_linear(const std::array<float, In>& input, const std::vector<float>& weights,
                          const std::array<float, Out>& biases, std::array<float, Out>& output) noexcept
        {
            for (std::size_t row = 0; row < Out; ++row)
            {
                const auto begin = weights.begin() + static_cast<std::ptrdiff_t>(row * In);
                output[row] = biases[row] + std::inner_product(input.begin(), input.end(), begin, 0.0f);
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
        std::vector<float> feature_weights;
        std::array<float, kAccumulatorSize> feature_biases{};
        std::vector<float> fc1_weights;
        std::array<float, kLayer2Size> fc1_biases{};
        std::vector<float> fc2_weights;
        std::array<float, kLayer3Size> fc2_biases{};
        std::vector<float> output_weights;
        std::array<float, 1> output_bias{};
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

        std::vector<std::int16_t> raw_feature_weights(kFeatureCount * kAccumulatorSize);
        input.read(reinterpret_cast<char*>(raw_feature_weights.data()),
                   static_cast<std::streamsize>(sizeof(std::int16_t) * raw_feature_weights.size()));
        if (input.fail())
        {
            return false;
        }

        for (std::size_t i = 0; i < raw_feature_weights.size(); ++i)
        {
            next->feature_weights[i] = static_cast<float>(raw_feature_weights[i]) / kConversionFactor;
        }

        next->feature_biases = read_float_array<kAccumulatorSize>(input);
        if (!read_floats(input, next->fc1_weights))
        {
            return false;
        }
        next->fc1_biases = read_float_array<kLayer2Size>(input);
        if (!read_floats(input, next->fc2_weights))
        {
            return false;
        }
        next->fc2_biases = read_float_array<kLayer3Size>(input);
        if (!read_floats(input, next->output_weights))
        {
            return false;
        }
        next->output_bias = read_float_array<1>(input);

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
                                      PieceType piece_type, Square square, float sign) const noexcept
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
        std::array<float, kLayer1InputSize> transformed{};
        for (std::size_t i = 0; i < kAccumulatorSize; ++i)
        {
            transformed[i] = std::clamp(accumulator.values[color_index(side)][i], 0.0f, 1.0f);
            transformed[i + kAccumulatorSize] = std::clamp(accumulator.values[color_index(other)][i], 0.0f, 1.0f);
        }

        std::array<float, kLayer2Size> layer1{};
        std::array<float, kLayer3Size> layer2{};
        std::array<float, 1> output{};
        dense_clipped_relu(transformed, network_->fc1_weights, network_->fc1_biases, layer1);
        dense_clipped_relu(layer1, network_->fc2_weights, network_->fc2_biases, layer2);
        dense_linear(layer2, network_->output_weights, network_->output_bias, output);
        return static_cast<Score>(output[0] * 100.0f);
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
                        apply_feature(accumulator, perspective, piece_color, type, square, 1.0f);
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
                apply_feature(next, perspective, piece_color(piece), piece_type(piece), dirty.removed_squares[i],
                              -1.0f);
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
                apply_feature(next, perspective, piece_color(piece), piece_type(piece), dirty.added_squares[i], 1.0f);
            }
        }

        next.valid = true;
    }

} // namespace aurora::chess
