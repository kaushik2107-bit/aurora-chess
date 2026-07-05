#pragma once

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::chess
{

    class Engine;

    struct SetOptionCommand
    {
        std::string name;
        std::string value;
    };

    enum class UciOptionType
    {
        Check,
        Spin,
        Button,
        String,
    };

    class UciOption
    {
    public:
        [[nodiscard]] static UciOption check(bool value);
        [[nodiscard]] static UciOption spin(int value, int min, int max);
        [[nodiscard]] static UciOption button();
        [[nodiscard]] static UciOption string(std::string value);

        [[nodiscard]] UciOptionType type() const noexcept;
        [[nodiscard]] std::string_view type_name() const noexcept;
        [[nodiscard]] std::string_view default_value() const noexcept;
        [[nodiscard]] std::string_view value() const noexcept;
        [[nodiscard]] int min() const noexcept;
        [[nodiscard]] int max() const noexcept;
        [[nodiscard]] bool as_bool() const noexcept;
        [[nodiscard]] int as_int() const noexcept;
        [[nodiscard]] bool set(std::string_view value);
        void write(std::ostream& output, std::string_view name) const;

    private:
        UciOption(UciOptionType type, std::string value, int min = 0, int max = 0);

        UciOptionType type_{UciOptionType::Button};
        std::string default_value_;
        std::string value_;
        int min_{0};
        int max_{0};
    };

    class UciOptions
    {
    public:
        UciOptions();

        [[nodiscard]] bool debug() const noexcept;
        [[nodiscard]] bool ponder() const noexcept;
        [[nodiscard]] std::size_t hash_mb() const noexcept;
        [[nodiscard]] int move_overhead_ms() const noexcept;

        void set_debug(bool enabled) noexcept;
        [[nodiscard]] std::optional<SetOptionCommand> parse_setoption(std::string_view command) const;
        [[nodiscard]] bool apply(Engine& engine, const SetOptionCommand& option);
        void write(std::ostream& output, const Engine& engine) const;

    private:
        using OnChange = std::function<void(Engine&, const UciOption&)>;

        struct Entry
        {
            std::string name;
            UciOption option;
            OnChange on_change;
        };

        [[nodiscard]] Entry* find(std::string_view name) noexcept;
        [[nodiscard]] const Entry* find(std::string_view name) const noexcept;

        std::vector<Entry> options_;
        bool debug_{false};
    };

} // namespace aurora::chess
