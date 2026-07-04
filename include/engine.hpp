#pragma once

#include <string>
#include <string_view>

#include "board.hpp"
#include "movegen.hpp"
#include "nnue.hpp"
#include "search.hpp"
#include "thread.hpp"

namespace aurora::chess
{

    class Engine
    {
    public:
        explicit Engine(std::string_view name = "Aurora");

        [[nodiscard]] std::string_view name() const noexcept;
        [[nodiscard]] std::string version() const noexcept;
        [[nodiscard]] std::string describe() const noexcept;
        [[nodiscard]] bool nnue_loaded() const noexcept;
        [[nodiscard]] std::string_view nnue_path() const noexcept;

        void set_position(std::string_view fen);
        bool make_move(Move move);
        void new_game();
        void clear_hash();
        void set_hash_size_mb(std::size_t megabytes);
        void set_thread_count(std::size_t threads);
        [[nodiscard]] std::size_t thread_count() const noexcept;
        [[nodiscard]] const Board& board() const noexcept;
        [[nodiscard]] MoveList legal_moves() const;
        [[nodiscard]] Score evaluate() const noexcept;
        [[nodiscard]] std::uint64_t perft(std::size_t depth) const;
        [[nodiscard]] SearchResult search(SearchLimits limits) const;

    private:
        std::string name_;
        Board board_;
        NnueEvaluator evaluator_;
        mutable TranspositionTable ttable_;
        mutable ThreadPool threads_;
    };

} // namespace aurora::chess
