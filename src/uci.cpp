#include "uci.hpp"

#include "helpers.hpp"
#include "movegen.hpp"
#include "perft.hpp"
#include "speed.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace aurora::chess
{
    namespace
    {

        [[nodiscard]] std::string square_to_string(Square square)
        {
            const auto index = static_cast<int>(square);
            std::string text;
            text.push_back(static_cast<char>('a' + (index % 8)));
            text.push_back(static_cast<char>('1' + (index / 8)));
            return text;
        }

        [[nodiscard]] char promotion_suffix(MoveFlag flag) noexcept
        {
            switch (flag)
            {
            case MoveFlag::KnightPromotion:
            case MoveFlag::KnightPromotionCapture:
                return 'n';
            case MoveFlag::BishopPromotion:
            case MoveFlag::BishopPromotionCapture:
                return 'b';
            case MoveFlag::RookPromotion:
            case MoveFlag::RookPromotionCapture:
                return 'r';
            case MoveFlag::QueenPromotion:
            case MoveFlag::QueenPromotionCapture:
                return 'q';
            default:
                return '\0';
            }
        }

        [[nodiscard]] std::string move_to_uci(Move move)
        {
            std::string text = square_to_string(move_from(move)) + square_to_string(move_to(move));
            const char promotion = promotion_suffix(move_flag(move));
            if (promotion != '\0')
            {
                text.push_back(promotion);
            }
            return text;
        }

        [[nodiscard]] std::uint64_t nodes_per_second(std::uint64_t nodes, std::chrono::steady_clock::duration elapsed) noexcept
        {
            const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
            if (elapsed_ns <= 0)
            {
                return 0;
            }
            return static_cast<std::uint64_t>((static_cast<long double>(nodes) * 1'000'000'000.0L) / static_cast<long double>(elapsed_ns));
        }

        void print_speed_header(std::ostream &output)
        {
            output << std::left
                   << std::setw(8) << "Depth"
                   << std::setw(14) << "Nodes"
                   << std::setw(12) << "Captures"
                   << std::setw(8) << "E.p."
                   << std::setw(10) << "Castles"
                   << std::setw(12) << "Promotions"
                   << std::setw(10) << "Checks"
                   << std::setw(12) << "Checkmates"
                   << std::setw(14) << "NPS" << '\n'
                   << std::flush;
        }

        void print_speed_row(std::ostream &output, const SpeedStats &stats, std::uint64_t nps)
        {
            output << std::left
                   << std::setw(8) << stats.depth
                   << std::setw(14) << stats.nodes
                   << std::setw(12) << stats.captures
                   << std::setw(8) << stats.en_passant
                   << std::setw(10) << stats.castles
                   << std::setw(12) << stats.promotions
                   << std::setw(10) << stats.checks
                   << std::setw(12) << stats.checkmates
                   << std::setw(14) << nps << '\n'
                   << std::flush;
        }

    } // namespace

    void run_uci_loop(Engine &engine, std::istream &input, std::ostream &output)
    {
        std::string line;
        while (std::getline(input, line))
        {
            const std::string command = trim(line);
            if (command.empty())
            {
                continue;
            }

            if (command == "isready")
            {
                output << "readyok\n";
            }
            else if (command == "uci")
            {
                output << "id name " << engine.name() << "\nid author Aurora\nuciok\n";
            }
            else if (command.rfind("position ", 0) == 0)
            {
                const auto fen = command.substr(9);
                if (!fen.empty())
                {
                    engine.set_position(fen);
                }
            }
            else if (command.rfind("go perft ", 0) == 0)
            {
                const auto depth_text = command.substr(9);
                std::istringstream input(depth_text);
                std::size_t depth = 0;
                input >> depth;

                if (depth == 0)
                {
                    output << "\nNodes searched: 1\n";
                    continue;
                }

                std::uint64_t nodes = 0;
                const auto start = std::chrono::steady_clock::now();
                const auto moves = MoveGenerator{}.generate(engine.board());
                for (const auto &entry : moves)
                {
                    if (entry.move == 0)
                    {
                        continue;
                    }

                    Board next = engine.board();
                    if (!next.make_move(entry.move))
                    {
                        continue;
                    }

                    const auto count = depth == 1 ? 1 : perft(next, depth - 1);
                    output << move_to_uci(entry.move) << ": " << count << '\n' << std::flush;
                    nodes += count;
                }
                const auto elapsed = std::chrono::steady_clock::now() - start;
                output << "\nNodes searched: " << nodes << '\n'
                       << "NPS: " << nodes_per_second(nodes, elapsed) << '\n'
                       << std::flush;
            }
            else if (command.rfind("go speed ", 0) == 0)
            {
                const auto depth_text = command.substr(9);
                std::istringstream input(depth_text);
                std::size_t depth = 0;
                input >> depth;

                print_speed_header(output);
                for (std::size_t current_depth = 1; current_depth <= depth; ++current_depth)
                {
                    const auto start = std::chrono::steady_clock::now();
                    const auto stats = speed_stats_at_depth(engine.board(), current_depth);
                    const auto elapsed = std::chrono::steady_clock::now() - start;
                    print_speed_row(output, stats, nodes_per_second(stats.nodes, elapsed));
                }
            }
            else if (command == "quit")
            {
                break;
            }
        }
    }

} // namespace aurora::chess
