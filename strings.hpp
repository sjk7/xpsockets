#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>

namespace strings {

[[maybe_unused]] static inline constexpr const char* DBLNEWLINE = "\r\n\r\n";
[[maybe_unused]] static inline constexpr const char* TAB = "\t";
[[maybe_unused]] static inline constexpr const char* NEWLINE = "\r\n";
[[maybe_unused]] static inline constexpr const char* UNIX_NEWLINE = "\n";
[[maybe_unused]] static inline constexpr const char* COLON = ":";
[[maybe_unused]] static inline constexpr const char* SPACE = " ";

inline bool is_a_space(const int ch) noexcept {
    return std::isspace(ch); // if (ch == 32 || ch == 160 || ch == '\n' || ch ==
                             // '\r' || ch == '\t') return true;
    // return false;
}

// trim from both ends
template <typename F>
inline std::string& trim(std::string& s, F&& f = is_a_space) {
    s.erase(std::remove_if(s.begin(), s.end(), f), s.end());
    return s;
}

template <typename S = std::string_view, typename F = decltype(is_a_space)>
inline std::string_view& trimSV(std::string_view& s, F&& f = is_a_space) {

    auto it = std::find_if_not(s.begin(), s.end(), f);
    if (it != s.end()) {
        auto pos = it - s.begin();
        s.remove_prefix(pos);
    }
    const auto rit = std::find_if_not(s.rbegin(), s.rend(), f);
    if (rit != s.rend()) {
        auto pos = s.size() - (s.rend() - rit);
        s.remove_suffix(pos);
    }
    return s;
}

inline std::vector<std::string_view> splitSV(
    std::string_view strv, std::string_view delims = " ") {
    std::vector<std::string_view> output;
    // output.reserve(strv.length() / 4);
    auto first = strv.begin();

    while (first != strv.end()) {
        const auto second = std::find_first_of(
            first, std::cend(strv), std::cbegin(delims), std::cend(delims));
        // std::cout << first << ", " << second << '\n';
        if (first != second) {
            output.emplace_back(strv.substr(std::distance(strv.begin(), first),
                std::distance(first, second)));
        }

        if (second == strv.end()) break;

        first = std::next(second);
    }

    return output;
}

inline std::vector<std::string> split(
    const std::string& str, const std::string& delims = " ") {
    std::vector<std::string> output;
    auto first = std::cbegin(str);

    while (first != std::cend(str)) {
        const auto second = std::find_first_of(
            first, std::cend(str), std::cbegin(delims), std::cend(delims));

        if (first != second) output.emplace_back(first, second);

        if (second == std::cend(str)) break;

        first = std::next(second);
    }

    return output;
}

std::string to_lower(const std::string& s) {
    std::string data(s);
    std::transform(data.begin(), data.end(), data.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return data;
}

std::string to_lower(const std::string_view s) {
    std::string data(s);
    std::transform(data.begin(), data.end(), data.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return data;
}

} // namespace strings
