// The serial console's line parser.
//
// Here rather than in main/ because it turns outside bytes into meaning, so it
// belongs where a host test can reach it. The transport stays in main/console/.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace autolee::console {

enum class Command {
  kNone,     // a blank line
  kUnknown,  // a word we do not have
  kHelp,
  kWifiScan,
  kWifiInfo,
};

namespace detail {

inline bool is_space(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

inline std::string_view head(std::string_view text) {
  while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
  size_t end = 0;
  while (end < text.size() && !is_space(text[end])) end++;
  return text.substr(0, end);
}

inline bool equals_ignoring_case(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

}  // namespace detail

// Matches the first word only, ignoring case and surrounding whitespace;
// anything after it is ignored, since none of the commands take arguments.
inline Command parse_command(std::string_view line) {
  const std::string_view word = detail::head(line);
  if (word.empty()) return Command::kNone;
  if (detail::equals_ignoring_case(word, "help") || word == "?") return Command::kHelp;
  if (detail::equals_ignoring_case(word, "wifi-scan")) return Command::kWifiScan;
  if (detail::equals_ignoring_case(word, "wifi-info")) return Command::kWifiInfo;
  return Command::kUnknown;
}

// The word a line started with, for echoing back what was not understood.
inline std::string first_word(std::string_view line) {
  return std::string(detail::head(line));
}

}  // namespace autolee::console
