#include "engine.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace aurora::chess
{
    namespace
    {

        std::string trim(std::string value)
        {
            auto begin = std::find_if(value.begin(), value.end(), [](unsigned char ch)
                                      { return std::isspace(ch) == 0; });
            auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch)
                                    { return std::isspace(ch) == 0; })
                           .base();
            return std::string(begin, end);
        }

        std::uint64_t perft_recursive(const Board &board, std::size_t depth)
        {
            if (depth == 0)
            {
                return 1;
            }

            const auto moves = MoveGenerator{}.generate(board);
            std::uint64_t nodes = 0;
            for (const auto &entry : moves)
            {
                if (entry.move == 0)
                {
                    continue;
                }
                Board next = board;
                nodes += perft_recursive(next, depth - 1);
            }
            return nodes;
        }

    } // namespace

    Engine::Engine(std::string_view name) noexcept : name_(name) {}

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
        return perft_recursive(board_, depth);
    }

    void Engine::run_uci_loop()
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            const std::string command = trim(line);
            if (command.empty())
            {
                continue;
            }

            if (command == "isready")
            {
                std::cout << "readyok\n";
            }
            else if (command == "uci")
            {
                std::cout << "id name " << name_ << "\nid author Aurora\nuciok\n";
            }
            else if (command.rfind("position ", 0) == 0)
            {
                const auto fen = command.substr(9);
                if (!fen.empty())
                {
                    set_position(fen);
                }
            }
            else if (command.rfind("go perft ", 0) == 0)
            {
                const auto depth_text = command.substr(9);
                std::istringstream input(depth_text);
                std::size_t depth = 0;
                input >> depth;
                std::cout << "perft " << depth << " " << perft(depth) << "\n";
            }
            else if (command == "quit")
            {
                break;
            }
        }
    }
} // namespace aurora::chess
