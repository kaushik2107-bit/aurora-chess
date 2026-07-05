#include "ucioptions.hpp"

#include "engine.hpp"
#include "helpers.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ostream>
#include <sstream>
#include <utility>

namespace aurora::chess
{
    namespace
    {

        [[nodiscard]] bool equal_uci_name(std::string_view lhs, std::string_view rhs) noexcept
        {
            return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                                                          [](char a, char b)
                                                          {
                                                              return std::tolower(static_cast<unsigned char>(a)) ==
                                                                     std::tolower(static_cast<unsigned char>(b));
                                                          });
        }

        [[nodiscard]] std::string lower_copy(std::string_view text)
        {
            std::string value{text};
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
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

    } // namespace

    UciOption::UciOption(UciOptionType type, std::string value, int min, int max)
        : type_(type), default_value_(std::move(value)), value_(default_value_), min_(min), max_(max)
    {
    }

    UciOption UciOption::check(bool value)
    {
        return UciOption{UciOptionType::Check, value ? "true" : "false"};
    }

    UciOption UciOption::spin(int value, int min, int max)
    {
        return UciOption{UciOptionType::Spin, std::to_string(value), min, max};
    }

    UciOption UciOption::button()
    {
        return UciOption{UciOptionType::Button, {}};
    }

    UciOption UciOption::string(std::string value)
    {
        return UciOption{UciOptionType::String, std::move(value)};
    }

    UciOptionType UciOption::type() const noexcept
    {
        return type_;
    }

    std::string_view UciOption::type_name() const noexcept
    {
        switch (type_)
        {
        case UciOptionType::Check:
            return "check";
        case UciOptionType::Spin:
            return "spin";
        case UciOptionType::Button:
            return "button";
        case UciOptionType::String:
            return "string";
        }
        return "button";
    }

    std::string_view UciOption::default_value() const noexcept
    {
        return default_value_;
    }

    std::string_view UciOption::value() const noexcept
    {
        return value_;
    }

    int UciOption::min() const noexcept
    {
        return min_;
    }

    int UciOption::max() const noexcept
    {
        return max_;
    }

    bool UciOption::as_bool() const noexcept
    {
        return value_ == "true";
    }

    int UciOption::as_int() const noexcept
    {
        int result = 0;
        std::from_chars(value_.data(), value_.data() + value_.size(), result);
        return result;
    }

    bool UciOption::set(std::string_view value)
    {
        const std::string trimmed = trim(std::string{value});
        switch (type_)
        {
        case UciOptionType::Button:
            return true;
        case UciOptionType::Check:
        {
            const std::string lower = lower_copy(trimmed);
            if (lower == "true" || lower == "1" || lower == "on" || lower == "yes")
            {
                value_ = "true";
                return true;
            }
            if (lower == "false" || lower == "0" || lower == "off" || lower == "no")
            {
                value_ = "false";
                return true;
            }
            return false;
        }
        case UciOptionType::Spin:
        {
            const auto parsed = parse_int(trimmed);
            if (!parsed || *parsed < min_ || *parsed > max_)
            {
                return false;
            }
            value_ = std::to_string(*parsed);
            return true;
        }
        case UciOptionType::String:
            value_ = trimmed == "<empty>" ? std::string{} : trimmed;
            return true;
        }
        return false;
    }

    void UciOption::write(std::ostream& output, std::string_view name) const
    {
        output << "option name " << name << " type " << type_name();
        if (type_ == UciOptionType::Check)
        {
            output << " default " << default_value_;
        }
        else if (type_ == UciOptionType::Spin)
        {
            output << " default " << default_value_ << " min " << min_ << " max " << max_;
        }
        else if (type_ == UciOptionType::String)
        {
            output << " default " << (default_value_.empty() ? "<empty>" : default_value_);
        }
        output << '\n';
    }

    UciOptions::UciOptions()
    {
        options_.push_back(Entry{
            "Hash",
            UciOption::spin(16, 1, 1024),
            [](Engine& engine, const UciOption& option)
            { engine.set_hash_size_mb(static_cast<std::size_t>(option.as_int())); },
        });
        options_.push_back(Entry{
            "Clear Hash",
            UciOption::button(),
            [](Engine& engine, const UciOption&) { engine.clear_hash(); },
        });
        options_.push_back(Entry{
            "Ponder",
            UciOption::check(true),
            {},
        });
        options_.push_back(Entry{
            "Move Overhead",
            UciOption::spin(20, 0, 5000),
            {},
        });
        options_.push_back(Entry{
            "Use NNUE",
            UciOption::check(true),
            [](Engine& engine, const UciOption& option) { engine.set_use_nnue(option.as_bool()); },
        });
        options_.push_back(Entry{
            "Threads",
            UciOption::spin(1, 1, 128),
            [](Engine& engine, const UciOption& option)
            { engine.set_thread_count(static_cast<std::size_t>(option.as_int())); },
        });
    }

    bool UciOptions::debug() const noexcept
    {
        return debug_;
    }

    bool UciOptions::ponder() const noexcept
    {
        const Entry* entry = find("Ponder");
        return entry != nullptr && entry->option.as_bool();
    }

    std::size_t UciOptions::hash_mb() const noexcept
    {
        const Entry* entry = find("Hash");
        return entry == nullptr ? 16 : static_cast<std::size_t>(entry->option.as_int());
    }

    int UciOptions::move_overhead_ms() const noexcept
    {
        const Entry* entry = find("Move Overhead");
        return entry == nullptr ? 20 : entry->option.as_int();
    }

    void UciOptions::set_debug(bool enabled) noexcept
    {
        debug_ = enabled;
    }

    UciOptions::Entry* UciOptions::find(std::string_view name) noexcept
    {
        const auto it = std::find_if(options_.begin(), options_.end(),
                                     [name](const Entry& entry) { return equal_uci_name(entry.name, name); });
        return it == options_.end() ? nullptr : &*it;
    }

    const UciOptions::Entry* UciOptions::find(std::string_view name) const noexcept
    {
        const auto it = std::find_if(options_.begin(), options_.end(),
                                     [name](const Entry& entry) { return equal_uci_name(entry.name, name); });
        return it == options_.end() ? nullptr : &*it;
    }

    std::optional<SetOptionCommand> UciOptions::parse_setoption(std::string_view command) const
    {
        std::istringstream stream{std::string{command}};
        std::string token;
        stream >> token;
        if (!equal_uci_name(token, "setoption"))
        {
            return std::nullopt;
        }

        stream >> token;
        if (!equal_uci_name(token, "name"))
        {
            return std::nullopt;
        }

        SetOptionCommand option;
        while (stream >> token && !equal_uci_name(token, "value"))
        {
            option.name += (option.name.empty() ? "" : " ") + token;
        }

        while (stream >> token)
        {
            option.value += (option.value.empty() ? "" : " ") + token;
        }

        if (option.name.empty())
        {
            return std::nullopt;
        }
        return option;
    }

    bool UciOptions::apply(Engine& engine, const SetOptionCommand& option)
    {
        Entry* entry = find(option.name);
        if (entry == nullptr || !entry->option.set(option.value))
        {
            return false;
        }

        if (entry->on_change)
        {
            entry->on_change(engine, entry->option);
        }
        return true;
    }

    void UciOptions::write(std::ostream& output, const Engine&) const
    {
        for (const Entry& entry : options_)
        {
            entry.option.write(output, entry.name);
        }
    }

} // namespace aurora::chess
