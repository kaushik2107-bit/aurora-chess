#include "uci.hpp"

#include "helpers.hpp"
#include "movegen.hpp"
#include "perft.hpp"
#include "speed.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

        [[nodiscard]] std::vector<std::string> split_words(std::string_view text)
        {
            std::istringstream stream{std::string{text}};
            std::vector<std::string> words;
            std::string word;
            while (stream >> word)
            {
                words.push_back(word);
            }
            return words;
        }

        [[nodiscard]] std::optional<int> parse_int(std::string_view text)
        {
            std::istringstream stream{std::string{text}};
            int value = 0;
            stream >> value;
            if (stream.fail())
            {
                return std::nullopt;
            }
            return value;
        }

        [[nodiscard]] std::optional<std::size_t> parse_size(std::string_view text)
        {
            if (const auto value = parse_int(text); value && *value >= 0)
            {
                return static_cast<std::size_t>(*value);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::uint64_t nodes_per_second(std::uint64_t nodes,
                                                     std::chrono::steady_clock::duration elapsed) noexcept
        {
            const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
            if (elapsed_ns <= 0)
            {
                return 0;
            }
            return static_cast<std::uint64_t>((static_cast<long double>(nodes) * 1'000'000'000.0L) /
                                              static_cast<long double>(elapsed_ns));
        }

        void print_speed_header(std::ostream& output)
        {
            output << std::left << std::setw(8) << "Depth" << std::setw(14) << "Nodes" << std::setw(12) << "Captures"
                   << std::setw(8) << "E.p." << std::setw(10) << "Castles" << std::setw(12) << "Promotions"
                   << std::setw(10) << "Checks" << std::setw(12) << "Checkmates" << std::setw(14) << "Cum NPS" << '\n'
                   << std::flush;
        }

        void print_speed_row(std::ostream& output, const SpeedStats& stats, std::uint64_t nps)
        {
            output << std::left << std::setw(8) << stats.depth << std::setw(14) << stats.nodes << std::setw(12)
                   << stats.captures << std::setw(8) << stats.en_passant << std::setw(10) << stats.castles
                   << std::setw(12) << stats.promotions << std::setw(10) << stats.checks << std::setw(12)
                   << stats.checkmates << std::setw(14) << nps << '\n'
                   << std::flush;
        }

        void print_search_info(std::ostream& output, const SearchIteration& iteration)
        {
            output << "info depth " << iteration.depth << " seldepth " << iteration.selective_depth << " score cp "
                   << iteration.score << " nodes " << iteration.nodes;
            if (!iteration.pv.empty())
            {
                output << " pv";
                for (const Move move : iteration.pv)
                {
                    output << ' ' << move_to_uci(move);
                }
            }
            else if (iteration.best_move != 0)
            {
                output << " pv " << move_to_uci(iteration.best_move);
            }
            output << '\n' << std::flush;
        }

        void print_board(std::ostream& output, const Board& board)
        {
            for (int rank = 7; rank >= 0; --rank)
            {
                output << rank + 1 << "  ";
                for (int file = 0; file < 8; ++file)
                {
                    output << piece_to_char(board.piece_on(static_cast<Square>(rank * 8 + file))) << ' ';
                }
                output << '\n';
            }
            output << "\n   a b c d e f g h\n"
                   << "Fen: " << board.fen() << '\n'
                   << "Key: " << board.key() << '\n'
                   << std::flush;
        }

        struct PositionCommand
        {
            std::string fen;
            std::vector<std::string> moves;
        };

        enum class GoMode
        {
            Search,
            Perft,
            Speed,
        };

        struct GoCommand
        {
            GoMode mode{GoMode::Search};
            std::size_t depth{4};
            std::size_t quiescence_depth{8};
            bool ponder{false};
            bool infinite{false};
            std::optional<int> white_time;
            std::optional<int> black_time;
            std::optional<int> white_increment;
            std::optional<int> black_increment;
            std::optional<int> moves_to_go;
            std::optional<int> move_time;
            std::optional<int> nodes;
        };

        struct SetOptionCommand
        {
            std::string name;
            std::string value;
        };

        struct UciState
        {
            bool debug{false};
            bool ponder{false};
            std::size_t hash_mb{16};
        };

        [[nodiscard]] std::optional<PositionCommand> parse_position(std::string_view command)
        {
            constexpr std::string_view prefix = "position ";
            if (command.rfind(prefix, 0) != 0)
            {
                return std::nullopt;
            }

            std::string value = trim(std::string{command.substr(prefix.size())});
            std::string move_text;
            PositionCommand position;
            if (value == "startpos" || value.rfind("startpos ", 0) == 0)
            {
                position.fen = std::string{kStartFen};
                move_text = trim(value.substr(std::string_view{"startpos"}.size()));
            }
            else
            {
                constexpr std::string_view fen_prefix = "fen ";
                if (value.rfind(std::string{fen_prefix}, 0) == 0)
                {
                    value.erase(0, fen_prefix.size());
                }

                const auto moves_pos = value.find(" moves ");
                position.fen = moves_pos == std::string::npos ? value : value.substr(0, moves_pos);
                move_text = moves_pos == std::string::npos ? std::string{} : value.substr(moves_pos + 1);
            }

            move_text = trim(move_text);
            if (move_text.rfind("moves", 0) == 0)
            {
                move_text.erase(0, std::string_view{"moves"}.size());
                std::istringstream stream{move_text};
                std::string move;
                while (stream >> move)
                {
                    position.moves.push_back(move);
                }
            }

            position.fen = trim(position.fen);
            if (position.fen.empty())
            {
                return std::nullopt;
            }
            return position;
        }

        [[nodiscard]] GoCommand parse_go(std::string_view command)
        {
            GoCommand go;
            const auto words = split_words(command.substr(std::string_view{"go"}.size()));
            for (std::size_t i = 0; i < words.size(); ++i)
            {
                const std::string& word = words[i];
                auto next_size = [&]() -> std::optional<std::size_t>
                {
                    if (i + 1 >= words.size())
                    {
                        return std::nullopt;
                    }
                    ++i;
                    return parse_size(words[i]);
                };
                auto next_int = [&]() -> std::optional<int>
                {
                    if (i + 1 >= words.size())
                    {
                        return std::nullopt;
                    }
                    ++i;
                    return parse_int(words[i]);
                };

                if (word == "perft")
                {
                    go.mode = GoMode::Perft;
                    if (const auto depth = next_size())
                    {
                        go.depth = *depth;
                    }
                }
                else if (word == "speed")
                {
                    go.mode = GoMode::Speed;
                    if (const auto depth = next_size())
                    {
                        go.depth = *depth;
                    }
                }
                else if (word == "depth")
                {
                    if (const auto depth = next_size())
                    {
                        go.depth = *depth;
                    }
                }
                else if (word == "wtime")
                {
                    go.white_time = next_int();
                }
                else if (word == "btime")
                {
                    go.black_time = next_int();
                }
                else if (word == "winc")
                {
                    go.white_increment = next_int();
                }
                else if (word == "binc")
                {
                    go.black_increment = next_int();
                }
                else if (word == "movestogo")
                {
                    go.moves_to_go = next_int();
                }
                else if (word == "movetime")
                {
                    go.move_time = next_int();
                }
                else if (word == "nodes")
                {
                    go.nodes = next_int();
                }
                else if (word == "ponder")
                {
                    go.ponder = true;
                }
                else if (word == "infinite")
                {
                    go.infinite = true;
                }
            }

            go.depth = std::max<std::size_t>(1, go.depth);
            return go;
        }

        [[nodiscard]] std::optional<SetOptionCommand> parse_setoption(std::string_view command)
        {
            std::string rest = trim(std::string{command.substr(std::string_view{"setoption"}.size())});
            constexpr std::string_view name_prefix = "name ";
            if (rest.rfind(name_prefix, 0) != 0)
            {
                return std::nullopt;
            }

            rest.erase(0, name_prefix.size());
            const auto value_pos = rest.find(" value ");
            SetOptionCommand option;
            if (value_pos == std::string::npos)
            {
                option.name = trim(rest);
            }
            else
            {
                option.name = trim(rest.substr(0, value_pos));
                option.value = trim(rest.substr(value_pos + std::string_view{" value "}.size()));
            }

            if (option.name.empty())
            {
                return std::nullopt;
            }
            return option;
        }

        bool play_uci_move(Engine& engine, std::string_view move_text)
        {
            const auto moves = engine.legal_moves();
            for (const auto& entry : moves)
            {
                if (move_to_uci(entry.move) == move_text)
                {
                    return engine.make_move(entry.move);
                }
            }
            return false;
        }

        void print_uci(std::ostream& output, const Engine& engine, const UciState& state)
        {
            output << "id name " << engine.name() << '\n'
                   << "id author Aurora\n"
                   << "option name Hash type spin default " << state.hash_mb << " min 1 max 1024\n"
                   << "option name Clear Hash type button\n"
                   << "option name Ponder type check default false\n"
                   << "option name Threads type spin default " << engine.thread_count() << " min 1 max 128\n";
            if (engine.nnue_loaded())
            {
                output << "info string NNUE evaluation using " << engine.nnue_path() << '\n';
            }
            else
            {
                output << "info string NNUE evaluation unavailable, using PSQT\n";
            }
            output << "uciok\n" << std::flush;
        }

        void apply_option(Engine& engine, UciState& state, const SetOptionCommand& option)
        {
            std::string name = option.name;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            if (name == "hash")
            {
                if (const auto hash = parse_size(option.value))
                {
                    state.hash_mb = std::clamp<std::size_t>(*hash, 1, 1024);
                    engine.set_hash_size_mb(state.hash_mb);
                }
            }
            else if (name == "clear hash")
            {
                engine.clear_hash();
            }
            else if (name == "ponder")
            {
                state.ponder = option.value == "true" || option.value == "1";
            }
            else if (name == "threads")
            {
                if (const auto threads = parse_size(option.value))
                {
                    engine.set_thread_count(std::clamp<std::size_t>(*threads, 1, 128));
                }
            }
        }

        void run_perft_command(const Engine& engine, std::ostream& output, std::size_t depth)
        {
            if (depth == 0)
            {
                output << "\nNodes searched: 1\n";
                return;
            }

            std::uint64_t nodes = 0;
            const auto start = std::chrono::steady_clock::now();
            Board board = engine.board();
            const auto moves = MoveGenerator{}.generate(board);
            for (const auto& entry : moves)
            {
                if (!board.make_move(entry.move))
                {
                    continue;
                }

                const auto count = depth == 1 ? 1 : perft(board, depth - 1);
                output << move_to_uci(entry.move) << ": " << count << '\n' << std::flush;
                nodes += count;
                board.undo_move();
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            output << "\nNodes searched: " << nodes << '\n'
                   << "NPS: " << nodes_per_second(nodes, elapsed) << '\n'
                   << std::flush;
        }

        void run_speed_command(const Engine& engine, std::ostream& output, std::size_t depth)
        {
            print_speed_header(output);
            std::uint64_t cumulative_nodes = 0;
            const auto cumulative_start = std::chrono::steady_clock::now();
            for (std::size_t current_depth = 1; current_depth <= depth; ++current_depth)
            {
                const auto stats = speed_stats_at_depth(engine.board(), current_depth);
                cumulative_nodes += stats.nodes;
                const auto elapsed = std::chrono::steady_clock::now() - cumulative_start;
                print_speed_row(output, stats, nodes_per_second(cumulative_nodes, elapsed));
            }
        }

        class SearchController
        {
        public:
            ~SearchController()
            {
                stop_and_wait();
            }

            SearchController(const SearchController&) = delete;
            SearchController& operator=(const SearchController&) = delete;

            SearchController() = default;

            void start(Engine& engine, std::ostream& output, std::mutex& output_mutex, GoCommand go)
            {
                stop_and_wait();
                stop_.store(false, std::memory_order_relaxed);
                infinite_ = go.infinite || go.ponder;
                worker_ = std::thread(
                    [this, &engine, &output, &output_mutex, go]
                    {
                        SearchLimits limits;
                        limits.depth = go.depth;
                        limits.quiescence_depth = go.quiescence_depth;
                        limits.stop = &stop_;
                        std::optional<SearchIteration> last_reported;
                        limits.on_iteration = [&output, &output_mutex, &last_reported](const SearchIteration& iteration)
                        {
                            last_reported = iteration;
                            std::lock_guard lock{output_mutex};
                            print_search_info(output, iteration);
                        };

                        const auto result = engine.search(limits);
                        Move best_move = result.best_move;
                        if (best_move == 0)
                        {
                            const auto moves = engine.legal_moves();
                            if (!moves.empty())
                            {
                                best_move = moves.begin()->move;
                            }
                        }
                        std::lock_guard lock{output_mutex};
                        if (!result.iterations.empty())
                        {
                            const SearchIteration& final_iteration = result.iterations.back();
                            const bool already_reported = last_reported.has_value() &&
                                                          last_reported->best_move == final_iteration.best_move &&
                                                          last_reported->score == final_iteration.score &&
                                                          last_reported->depth == final_iteration.depth &&
                                                          last_reported->pv == final_iteration.pv;
                            if (!already_reported)
                            {
                                print_search_info(output, final_iteration);
                            }
                        }
                        output << "bestmove " << (best_move == 0 ? "0000" : move_to_uci(best_move)) << '\n'
                               << std::flush;
                    });
            }

            void stop_and_wait()
            {
                stop_.store(true, std::memory_order_relaxed);
                wait();
                stop_.store(false, std::memory_order_relaxed);
            }

            void wait_or_stop_for_quit()
            {
                if (infinite_)
                {
                    stop_.store(true, std::memory_order_relaxed);
                }
                wait();
                stop_.store(false, std::memory_order_relaxed);
            }

            void wait()
            {
                if (worker_.joinable())
                {
                    worker_.join();
                }
                infinite_ = false;
            }

        private:
            std::atomic_bool stop_{false};
            std::thread worker_;
            bool infinite_{false};
        };

        void run_search_command(SearchController& search, Engine& engine, std::ostream& output,
                                std::mutex& output_mutex, const GoCommand& go)
        {
            search.start(engine, output, output_mutex, go);
        }

    } // namespace

    void run_uci_loop(Engine& engine, std::istream& input, std::ostream& output)
    {
        UciState state;
        std::mutex output_mutex;
        SearchController search;
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
                std::lock_guard lock{output_mutex};
                output << "readyok\n" << std::flush;
            }
            else if (command == "uci")
            {
                std::lock_guard lock{output_mutex};
                print_uci(output, engine, state);
            }
            else if (command == "ucinewgame")
            {
                search.stop_and_wait();
                engine.new_game();
            }
            else if (command.rfind("debug ", 0) == 0)
            {
                state.debug = command == "debug on";
            }
            else if (command.rfind("setoption ", 0) == 0)
            {
                search.stop_and_wait();
                if (const auto option = parse_setoption(command))
                {
                    apply_option(engine, state, *option);
                }
            }
            else if (command.rfind("position ", 0) == 0)
            {
                search.stop_and_wait();
                if (const auto position = parse_position(command))
                {
                    engine.set_position(position->fen);
                    for (const auto& move : position->moves)
                    {
                        if (!play_uci_move(engine, move))
                        {
                            break;
                        }
                    }
                }
            }
            else if (command == "d")
            {
                search.stop_and_wait();
                std::lock_guard lock{output_mutex};
                print_board(output, engine.board());
            }
            else if (command == "eval")
            {
                search.stop_and_wait();
                std::lock_guard lock{output_mutex};
                output << "info string eval " << engine.evaluate() << '\n' << std::flush;
            }
            else if (command.rfind("go", 0) == 0 && (command.size() == 2 || std::isspace(command[2]) != 0))
            {
                const GoCommand go = parse_go(command);
                switch (go.mode)
                {
                case GoMode::Perft:
                    search.stop_and_wait();
                    run_perft_command(engine, output, go.depth);
                    break;
                case GoMode::Speed:
                    search.stop_and_wait();
                    run_speed_command(engine, output, go.depth);
                    break;
                case GoMode::Search:
                    run_search_command(search, engine, output, output_mutex, go);
                    break;
                }
            }
            else if (command == "stop" || command == "ponderhit")
            {
                search.stop_and_wait();
            }
            else if (command == "quit")
            {
                search.wait_or_stop_for_quit();
                break;
            }
        }
    }

} // namespace aurora::chess
