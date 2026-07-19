#include "engine.hpp"
#include "perft.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace aurora::chess
{
    Engine::Engine(std::string_view name) : name_(name), ttable_(1 << 20), threads_(1)
    {
        const bool loaded = evaluator_.load_default();
        // Enable NNUE by default when a network is successfully loaded.
        use_nnue_ = loaded;
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

    bool Engine::use_nnue() const noexcept
    {
        return use_nnue_;
    }

    void Engine::set_position(std::string_view fen)
    {
        board_.set_fen(fen);
    }

    bool Engine::make_move(Move move)
    {
        return board_.make_move(move);
    }

    void Engine::new_game()
    {
        board_.set_fen(kStartFen);
        clear_hash();
    }

    void Engine::clear_hash()
    {
        ttable_.clear();
    }

    void Engine::set_hash_size_mb(std::size_t megabytes)
    {
        constexpr std::size_t kBytesPerMegabyte = 1024 * 1024;
        const std::size_t bytes = std::max<std::size_t>(1, megabytes) * kBytesPerMegabyte;
        const std::size_t entries = std::max<std::size_t>(1, bytes / TranspositionTable::kEntryBytes);
        ttable_.resize(entries);
    }

    void Engine::set_thread_count(std::size_t threads)
    {
        threads_.resize(threads);
    }

    void Engine::set_use_nnue(bool enabled) noexcept
    {
        use_nnue_ = enabled;
    }

    std::size_t Engine::thread_count() const noexcept
    {
        return threads_.size();
    }

    std::size_t Engine::hashfull() const noexcept
    {
        return ttable_.hashfull();
    }

    const Board& Engine::board() const noexcept
    {
        return board_;
    }

    MoveList Engine::legal_moves() const
    {
        return MoveGenerator{}.generate(board_);
    }

    Score Engine::evaluate() const noexcept
    {
        return use_nnue_ && evaluator_.is_loaded() ? evaluator_.evaluate(board_) : psqt_evaluator_.evaluate(board_);
    }

    std::uint64_t Engine::perft(std::size_t depth) const
    {
        return aurora::chess::perft(board_, depth);
    }

    SearchResult Engine::search(SearchLimits limits) const
    {
        limits.threads = thread_count();
        limits.thread_pool = &threads_;
        const Evaluator& active_evaluator = use_nnue_ && evaluator_.is_loaded()
                                                ? static_cast<const Evaluator&>(evaluator_)
                                                : static_cast<const Evaluator&>(psqt_evaluator_);
        return aurora::chess::search(board_, limits, ttable_, active_evaluator);
    }
} // namespace aurora::chess
