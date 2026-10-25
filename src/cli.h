#ifndef CLI_H
#define CLI_H

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

// The value of a numeric command-line option. A value that is not a whole number of type T, such as
// "abc" or "12x", ends the program with a message and exit code 2, as an unknown option does.
// (std::stoi would throw before main's try block, which aborts the program, and accepts "12x".)
template <class T>
T optionValue(const char *tool, const char *option, const std::string &text) {
  T value{};
  const char *end = text.data() + text.size();
  auto [stop, error] = std::from_chars(text.data(), end, value);
  if (text.empty() || error != std::errc() || stop != end) {
    std::fprintf(stderr, "%s: bad value for %s: '%s'\n", tool, option, text.c_str());
    std::exit(2);
  }
  return value;
}

#endif
