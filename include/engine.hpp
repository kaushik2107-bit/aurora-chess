#pragma once

#include <string>
#include <string_view>

#include "board.hpp"
#include "movegen.hpp"
#include "search.hpp"

namespace aurora::chess
{

    class Engine
    {
    public:
        explicit Engine(std::string_view name = "Aurora") noexcept;

        [[nodiscard]] std::string_view name() const noexcept;
        [[nodiscard]] std::string version() const noexcept;
        [[nodiscard]] std::string describe() const noexcept;

        void set_position(std::string_view fen);
        bool make_move(Move move);
        [[nodiscard]] const Board &board() const noexcept;
        [[nodiscard]] MoveList legal_moves() const;
        [[nodiscard]] std::uint64_t perft(std::size_t depth) const;
        [[nodiscard]] SearchResult search(SearchLimits limits) const;

    private:
        std::string name_;
        Board board_;
    };

} // namespace aurora::chess
