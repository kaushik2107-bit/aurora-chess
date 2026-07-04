#include "engine.hpp"

#include "pext.hpp"
#include "perft.hpp"

#include <string>
#include <string_view>

namespace aurora::chess
{
    namespace
    {

        void initialize_engine()
        {
            SlidingAttacks::init();
        }

    } // namespace

    Engine::Engine(std::string_view name) noexcept : name_(name)
    {
        initialize_engine();
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

    void Engine::set_position(std::string_view fen)
    {
        board_.set_fen(fen);
    }

    const Board &Engine::board() const noexcept
    {
        return board_;
    }
    std::array<MoveEntry, 256> Engine::legal_moves() const
    {
        return MoveGenerator{}.generate(board_);
    }

    std::uint64_t Engine::perft(std::size_t depth) const
    {
        return aurora::chess::perft(board_, depth);
    }
} // namespace aurora::chess
