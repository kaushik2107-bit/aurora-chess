#include "movepick.hpp"

#include "helpers.hpp"
#include "see.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace aurora::chess
{
    namespace
    {

        constexpr std::array<Score, static_cast<std::size_t>(PieceType::Count)> kPieceValues{
            100, 320, 330, 500, 900, 0,
        };
        constexpr Score kMaxMoveScore = 65535;

        [[nodiscard]] Score captured_piece_value(const Board& board, Move move) noexcept
        {
            if (move_flag(move) == MoveFlag::EnPassant)
            {
                return kPieceValues[static_cast<std::size_t>(PieceType::Pawn)];
            }

            const Piece captured = board.piece_on(move_to(move));
            return captured == Piece::None ? 0 : kPieceValues[static_cast<std::size_t>(piece_type(captured))];
        }

        [[nodiscard]] Score moving_piece_value(const Board& board, Move move) noexcept
        {
            const Piece moving = board.piece_on(move_from(move));
            return moving == Piece::None ? 0 : kPieceValues[static_cast<std::size_t>(piece_type(moving))];
        }

        [[nodiscard]] Score promotion_bonus(MoveFlag flag) noexcept
        {
            switch (flag)
            {
            case MoveFlag::QueenPromotion:
            case MoveFlag::QueenPromotionCapture:
                return 900;
            case MoveFlag::RookPromotion:
            case MoveFlag::RookPromotionCapture:
                return 500;
            case MoveFlag::BishopPromotion:
            case MoveFlag::BishopPromotionCapture:
                return 330;
            case MoveFlag::KnightPromotion:
            case MoveFlag::KnightPromotionCapture:
                return 320;
            default:
                return 0;
            }
        }

        [[nodiscard]] bool same_move(Move lhs, Move rhs) noexcept
        {
            return lhs != 0 && lhs == rhs;
        }

        [[nodiscard]] std::uint16_t move_score(Score score) noexcept
        {
            return static_cast<std::uint16_t>(std::clamp(score, Score{0}, kMaxMoveScore));
        }

        void sort_by_score(std::vector<MoveEntry>& moves)
        {
            std::sort(moves.begin(), moves.end(),
                      [](const MoveEntry& lhs, const MoveEntry& rhs) { return lhs.score < rhs.score; });
        }

    } // namespace

    bool is_noisy(Move move) noexcept
    {
        const MoveFlag flag = move_flag(move);
        return is_capture(flag) || is_promotion(flag);
    }

    bool is_good_noisy(const Board& board, Move move) noexcept
    {
        const MoveFlag flag = move_flag(move);
        if (is_capture(flag))
        {
            return see_ge(board, move, 0);
        }

        return is_promotion(flag);
    }

    MovePicker::MovePicker(const Board& board, MoveOrdering ordering) : board_(board), ordering_(ordering)
    {
        moves_ = MoveGenerator{}.generate(board_);
        generated_any_ = !moves_.empty();
        score_moves();
    }

    bool MovePicker::generated_any() const noexcept
    {
        return generated_any_;
    }

    bool MovePicker::contains(Move move) const noexcept
    {
        if (move == 0)
        {
            return false;
        }

        for (const auto& entry : moves_)
        {
            if (entry.move == move)
            {
                return true;
            }
        }

        return false;
    }

    Move MovePicker::next_from_scored(std::vector<MoveEntry>& moves)
    {
        if (moves.empty())
        {
            return 0;
        }

        const Move move = moves.back().move;
        moves.pop_back();
        return move;
    }

    void MovePicker::score_moves()
    {
        for (const auto& entry : moves_)
        {
            const Move move = entry.move;
            if (same_move(move, ordering_.tt_move))
            {
                continue;
            }

            if (same_move(move, ordering_.killers[0]) || same_move(move, ordering_.killers[1]))
            {
                continue;
            }

            if (is_noisy(move))
            {
                const Score score = promotion_bonus(move_flag(move)) + 10 * captured_piece_value(board_, move) -
                                    moving_piece_value(board_, move);
                (is_good_noisy(board_, move) ? good_noisy_ : bad_noisy_).push_back(MoveEntry{move, move_score(score)});
            }
            else
            {
                const Score score =
                    ordering_.history == nullptr ? 0 : (*ordering_.history)[static_cast<std::size_t>(move)];
                quiets_.push_back(MoveEntry{move, move_score(score)});
            }
        }

        sort_by_score(good_noisy_);
        sort_by_score(bad_noisy_);
        sort_by_score(quiets_);
    }

    Move MovePicker::next()
    {
        while (stage_ != Stage::Done)
        {
            switch (stage_)
            {
            case Stage::TtMove:
                stage_ = Stage::GoodNoisy;
                if (contains(ordering_.tt_move))
                {
                    return ordering_.tt_move;
                }
                break;
            case Stage::GoodNoisy:
                if (const Move move = next_from_scored(good_noisy_); move != 0)
                {
                    return move;
                }
                stage_ = Stage::Killers;
                break;
            case Stage::Killers:
                while (killer_index_ < ordering_.killers.size())
                {
                    const Move move = ordering_.killers[killer_index_++];
                    const bool duplicate_killer = killer_index_ > 1 && move == ordering_.killers[0];
                    if (!duplicate_killer && move != ordering_.tt_move && contains(move))
                    {
                        return move;
                    }
                }
                stage_ = Stage::Quiets;
                break;
            case Stage::Quiets:
                if (const Move move = next_from_scored(quiets_); move != 0)
                {
                    return move;
                }
                stage_ = Stage::BadNoisy;
                break;
            case Stage::BadNoisy:
                if (const Move move = next_from_scored(bad_noisy_); move != 0)
                {
                    return move;
                }
                stage_ = Stage::Done;
                break;
            case Stage::Done:
                break;
            }
        }

        return 0;
    }

} // namespace aurora::chess
