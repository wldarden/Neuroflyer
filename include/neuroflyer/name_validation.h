#pragma once
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace neuroflyer {

[[nodiscard]] inline bool is_valid_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;

    // Trim leading whitespace
    std::size_t start = 0;
    while (start < name.size() && name[start] == ' ') ++start;
    // Trim trailing whitespace
    std::size_t end = name.size();
    while (end > start && name[end - 1] == ' ') --end;
    // Reject names that are empty after trimming
    if (start >= end) return false;
    name = name.substr(start, end - start);

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))
            && c != ' ' && c != '-' && c != '_') {
            return false;
        }
    }

    // Check Windows reserved names
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    static constexpr std::array<std::string_view, 22> reserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    for (auto r : reserved) {
        if (upper == r) return false;
    }

    return true;
}

} // namespace neuroflyer
