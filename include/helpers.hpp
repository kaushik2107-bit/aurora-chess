#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

#include "board.hpp"

namespace aurora::chess
{

    [[nodiscard]] inline std::string trim(std::string value)
    {
        auto begin = std::find_if(value.begin(), value.end(), [](unsigned char ch)
                                  { return std::isspace(ch) == 0; });
        auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch)
                                { return std::isspace(ch) == 0; })
                       .base();
        return std::string(begin, end);
    }

    [[nodiscard]] constexpr bool has_castling(CastlingRights rights, CastlingRights flag) noexcept
    {
        return (static_cast<int>(rights) & static_cast<int>(flag)) != 0;
    }

    [[nodiscard]] constexpr bool is_capture(MoveFlag flag) noexcept
    {
        return flag == MoveFlag::Capture || flag == MoveFlag::EnPassant ||
               flag == MoveFlag::KnightPromotionCapture || flag == MoveFlag::BishopPromotionCapture ||
               flag == MoveFlag::RookPromotionCapture || flag == MoveFlag::QueenPromotionCapture;
    }

    [[nodiscard]] constexpr bool is_castle(MoveFlag flag) noexcept
    {
        return flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle;
    }

    template <typename MoveList>
    [[nodiscard]] std::size_t move_count(const MoveList &moves)
    {
        std::size_t count = 0;
        for (const auto &entry : moves)
        {
            if (entry.move != 0)
            {
                ++count;
            }
        }
        return count;
    }

} // namespace aurora::chess
