#include "uci.hpp"

#include "perft.hpp"
#include "speed.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace aurora::chess
{
    namespace
    {

        [[nodiscard]] std::string trim(std::string value)
        {
            auto begin = std::find_if(value.begin(), value.end(), [](unsigned char ch)
                                      { return std::isspace(ch) == 0; });
            auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch)
                                    { return std::isspace(ch) == 0; })
                           .base();
            return std::string(begin, end);
        }

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
                for (const auto &[move, count] : perft_divide(engine.board(), depth))
                {
                    output << move_to_uci(move) << ": " << count << '\n';
                    nodes += count;
                }
                output << "\nNodes searched: " << nodes << '\n';
            }
            else if (command.rfind("go speed ", 0) == 0)
            {
                const auto depth_text = command.substr(9);
                std::istringstream input(depth_text);
                std::size_t depth = 0;
                input >> depth;

                output << std::left
                       << std::setw(8) << "Depth"
                       << std::setw(14) << "Nodes"
                       << std::setw(12) << "Captures"
                       << std::setw(8) << "E.p."
                       << std::setw(10) << "Castles"
                       << std::setw(12) << "Promotions"
                       << std::setw(10) << "Checks"
                       << std::setw(12) << "Checkmates" << '\n';

                for (const auto &stats : speed_stats(engine.board(), depth))
                {
                    output << std::left
                           << std::setw(8) << stats.depth
                           << std::setw(14) << stats.nodes
                           << std::setw(12) << stats.captures
                           << std::setw(8) << stats.en_passant
                           << std::setw(10) << stats.castles
                           << std::setw(12) << stats.promotions
                           << std::setw(10) << stats.checks
                           << std::setw(12) << stats.checkmates << '\n';
                }
            }
            else if (command == "quit")
            {
                break;
            }
        }
    }

} // namespace aurora::chess
