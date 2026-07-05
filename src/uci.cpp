#include "uci.hpp"

#include "helpers.hpp"
#include "movegen.hpp"
#include "perft.hpp"
#include "speed.hpp"
#include "timeman.hpp"
#include "ucioptions.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
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

        void print_search_info(std::ostream& output, const SearchIteration& iteration,
                               std::chrono::steady_clock::duration elapsed)
        {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            output << "info depth " << iteration.depth << " seldepth " << iteration.selective_depth << " score cp "
                   << iteration.score << " nodes " << iteration.nodes << " nps "
                   << nodes_per_second(iteration.nodes, elapsed) << " time " << std::max<std::int64_t>(1, elapsed_ms);
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

        [[nodiscard]] std::int64_t deadline_ms(std::chrono::steady_clock::time_point time) noexcept
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
        }

        [[nodiscard]] Move ponder_move_from_result(const SearchResult& result, Move best_move) noexcept
        {
            if (best_move == 0 || result.pv.size() < 2 || result.pv.front() != best_move)
            {
                return 0;
            }
            return result.pv[1];
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
            bool depth_set{false};
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

        [[nodiscard]] TimeControl time_control_from_go(const GoCommand& go, int move_overhead_ms) noexcept
        {
            return TimeControl{
                go.white_time,  go.black_time, go.white_increment, go.black_increment,
                go.moves_to_go, go.move_time,  move_overhead_ms,
            };
        }

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
                        go.depth_set = true;
                    }
                }
                else if (word == "speed")
                {
                    go.mode = GoMode::Speed;
                    if (const auto depth = next_size())
                    {
                        go.depth = *depth;
                        go.depth_set = true;
                    }
                }
                else if (word == "depth")
                {
                    if (const auto depth = next_size())
                    {
                        go.depth = *depth;
                        go.depth_set = true;
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

        void print_uci(std::ostream& output, const Engine& engine, const UciOptions& options)
        {
            output << "id name " << engine.name() << '\n' << "id author Aurora\n";
            options.write(output, engine);
            output << "info string Options Hash=" << options.hash_mb() << "MB Threads=" << engine.thread_count()
                   << " Ponder=" << (options.ponder() ? "true" : "false") << '\n';
            if (engine.use_nnue() && engine.nnue_loaded())
            {
                output << "info string Evaluation NNUE active file " << engine.nnue_path() << '\n';
            }
            else if (engine.nnue_loaded())
            {
                output << "info string Evaluation PSQT active, NNUE available file " << engine.nnue_path() << '\n';
            }
            else
            {
                output << "info string Evaluation PSQT active\n";
            }
            output << "uciok\n" << std::flush;
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

            void start(Engine& engine, std::ostream& output, std::mutex& output_mutex, GoCommand go,
                       bool ponder_enabled, int move_overhead_ms)
            {
                stop_and_wait();
                stop_.store(false, std::memory_order_relaxed);
                deadline_ms_.store(0, std::memory_order_relaxed);
                const bool ponder_search = go.ponder && ponder_enabled;
                const auto search_start = std::chrono::steady_clock::now();
                const auto time_plan = go.infinite ? TimePlan{}
                                                   : plan_time(time_control_from_go(go, move_overhead_ms),
                                                               engine.board().side_to_move(), ponder_search);
                {
                    std::lock_guard lock{mutex_};
                    pondering_ = ponder_search;
                    ponder_released_ = !ponder_search;
                    infinite_ = go.infinite || ponder_search;
                    time_plan_ = time_plan;
                    managed_clock_start_ = search_start;
                }
                if (time_plan.active && !ponder_search)
                {
                    deadline_ms_.store(deadline_ms(search_start + time_plan.maximum), std::memory_order_relaxed);
                }
                worker_ = std::thread(
                    [this, &engine, &output, &output_mutex, go, ponder_enabled, ponder_search, time_plan, search_start]
                    {
                        constexpr std::size_t kOpenEndedSearchDepth = 64;
                        const bool use_open_depth = (go.infinite || ponder_search || time_plan.active) && !go.depth_set;
                        SearchLimits limits;
                        limits.depth = use_open_depth ? kOpenEndedSearchDepth : go.depth;
                        limits.quiescence_depth = go.quiescence_depth;
                        limits.stop = &stop_;
                        if (time_plan.active)
                        {
                            limits.shared_deadline_ms = &deadline_ms_;
                        }
                        limits.on_iteration = [this, &output, &output_mutex, search_start, time_plan,
                                               ponder_search](const SearchIteration& iteration)
                        {
                            const auto now = std::chrono::steady_clock::now();
                            const auto elapsed = now - search_start;
                            bool stop_on_soft_time = false;
                            if (time_plan.active)
                            {
                                std::lock_guard time_lock{mutex_};
                                const bool clock_running = !ponder_search || ponder_released_;
                                stop_on_soft_time = clock_running && now - managed_clock_start_ >= time_plan.optimum;
                            }

                            std::lock_guard lock{output_mutex};
                            print_search_info(output, iteration, elapsed);
                            if (stop_on_soft_time)
                            {
                                stop_.store(true, std::memory_order_relaxed);
                            }
                        };

                        const auto result = engine.search(limits);
                        wait_for_ponder_release();
                        Move best_move = result.best_move;
                        if (best_move == 0)
                        {
                            const auto moves = engine.legal_moves();
                            if (!moves.empty())
                            {
                                best_move = moves.begin()->move;
                            }
                        }
                        const Move ponder_move = ponder_enabled ? ponder_move_from_result(result, best_move) : 0;
                        std::lock_guard lock{output_mutex};
                        output << "bestmove " << (best_move == 0 ? "0000" : move_to_uci(best_move));
                        if (ponder_move != 0)
                        {
                            output << " ponder " << move_to_uci(ponder_move);
                        }
                        output << '\n' << std::flush;
                    });
            }

            void stop_and_wait()
            {
                stop_.store(true, std::memory_order_relaxed);
                release_ponder();
                wait();
                stop_.store(false, std::memory_order_relaxed);
            }

            void ponderhit()
            {
                const auto now = std::chrono::steady_clock::now();
                {
                    std::lock_guard lock{mutex_};
                    if (pondering_ && time_plan_.active)
                    {
                        managed_clock_start_ = now;
                        deadline_ms_.store(deadline_ms(now + time_plan_.maximum), std::memory_order_relaxed);
                    }
                    ponder_released_ = true;
                    pondering_ = false;
                    infinite_ = false;
                }
                cv_.notify_all();
            }

            void wait_or_stop_for_quit()
            {
                if (is_open_ended())
                {
                    stop_.store(true, std::memory_order_relaxed);
                }
                release_ponder();
                wait();
                stop_.store(false, std::memory_order_relaxed);
            }

            void wait()
            {
                if (worker_.joinable())
                {
                    worker_.join();
                }
                std::lock_guard lock{mutex_};
                infinite_ = false;
                pondering_ = false;
                ponder_released_ = true;
            }

        private:
            [[nodiscard]] bool is_open_ended() const
            {
                std::lock_guard lock{mutex_};
                return infinite_ || pondering_;
            }

            void release_ponder()
            {
                {
                    std::lock_guard lock{mutex_};
                    ponder_released_ = true;
                    pondering_ = false;
                    infinite_ = false;
                    time_plan_ = {};
                }
                deadline_ms_.store(0, std::memory_order_relaxed);
                cv_.notify_all();
            }

            void wait_for_ponder_release()
            {
                std::unique_lock lock{mutex_};
                cv_.wait(lock, [&] { return ponder_released_ || stop_.load(std::memory_order_relaxed); });
            }

            std::atomic_bool stop_{false};
            std::atomic<std::int64_t> deadline_ms_{0};
            mutable std::mutex mutex_;
            std::condition_variable cv_;
            std::thread worker_;
            TimePlan time_plan_{};
            std::chrono::steady_clock::time_point managed_clock_start_{};
            bool infinite_{false};
            bool pondering_{false};
            bool ponder_released_{true};
        };

        void run_search_command(SearchController& search, Engine& engine, std::ostream& output,
                                std::mutex& output_mutex, const GoCommand& go, const UciOptions& options)
        {
            search.start(engine, output, output_mutex, go, options.ponder(), options.move_overhead_ms());
        }

    } // namespace

    void run_uci_loop(Engine& engine, std::istream& input, std::ostream& output)
    {
        UciOptions options;
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
                print_uci(output, engine, options);
            }
            else if (command == "ucinewgame")
            {
                search.stop_and_wait();
                engine.new_game();
            }
            else if (command.rfind("debug ", 0) == 0)
            {
                options.set_debug(command == "debug on");
            }
            else if (command.rfind("setoption ", 0) == 0)
            {
                search.stop_and_wait();
                if (const auto option = options.parse_setoption(command))
                {
                    const bool applied = options.apply(engine, *option);
                    if (options.debug())
                    {
                        std::lock_guard lock{output_mutex};
                        output << "info string setoption " << option->name << (applied ? " ok" : " ignored") << '\n'
                               << std::flush;
                    }
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
                    run_search_command(search, engine, output, output_mutex, go, options);
                    break;
                }
            }
            else if (command == "stop")
            {
                search.stop_and_wait();
            }
            else if (command == "ponderhit")
            {
                search.ponderhit();
            }
            else if (command == "quit")
            {
                search.wait_or_stop_for_quit();
                break;
            }
        }
    }

} // namespace aurora::chess
