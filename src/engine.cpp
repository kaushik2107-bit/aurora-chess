#include "engine.hpp"
#include "perft.hpp"

#include <string>
#include <string_view>

namespace aurora::chess
{
    Engine::Engine(std::string_view name) : name_(name), ttable_(1 << 20)
    {
        [[maybe_unused]] const bool loaded = evaluator_.load_default();
    }

    std::string_view Engine::name() const noexcept
    {
        return name_;
    }

    std::string Engine::version() const noexcept
    {
        return "0.1.0";
    }

    std::string Engine::describe() const noexcept
    {
        return std::string{name_} + " Chess Engine v" + version();
    }

    bool Engine::nnue_loaded() const noexcept
    {
        return evaluator_.is_loaded();
    }

    std::string_view Engine::nnue_path() const noexcept
    {
        return evaluator_.path();
    }

    void Engine::set_position(std::string_view fen)
    {
        board_.set_fen(fen);
    }

    bool Engine::make_move(Move move)
    {
        return board_.make_move(move);
    }

    const Board& Engine::board() const noexcept
    {
        return board_;
    }
    MoveList Engine::legal_moves() const
    {
        return MoveGenerator{}.generate(board_);
    }

    std::uint64_t Engine::perft(std::size_t depth) const
    {
        return aurora::chess::perft(board_, depth);
    }

    SearchResult Engine::search(SearchLimits limits) const
    {
        return aurora::chess::search(board_, limits, ttable_, evaluator_);
    }
} // namespace aurora::chess
