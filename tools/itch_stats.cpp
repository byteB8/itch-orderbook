// Summary of an ITCH 5.0 file: message counts by type, the time span, the number of instruments,
// and any message whose length does not match the specification.
//
//   itch_stats <file>
#include "itch.h"
#include "itch_file.h"

#include <array>
#include <cstdio>
#include <exception>

namespace {

void printTime(const char *label, uint64_t ns) {
  std::printf("%s %02llu:%02llu:%02llu.%09llu\n", label, (unsigned long long)(ns / 3600000000000ull),
              (unsigned long long)(ns / 60000000000ull % 60), (unsigned long long)(ns / 1000000000ull % 60),
              (unsigned long long)(ns % 1000000000ull));
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: itch_stats <file>\n");
    return 2;
  }
  try {
    ItchFile file(argv[1]);
    std::array<uint64_t, 256> counts{};
    uint64_t first = 0, last = 0, badLength = 0;
    size_t n = file.forEach([&](const uint8_t *m, uint16_t len) {
      if (len == 0)
        return;
      ++counts[m[0]];
      uint16_t expected = itch::messageLength(char(m[0]));
      badLength += expected != 0 && expected != len;
      if (len >= 11) {
        uint64_t ts = itch::header(m).timestamp;
        if (first == 0)
          first = ts;
        last = ts;
      }
    });
    std::printf("%zu messages, %zu bytes\n", n, file.size());
    printTime("first", first);
    printTime("last ", last);
    std::printf("instruments (R messages): %llu\n", (unsigned long long)counts['R']);
    std::printf("length mismatches: %llu\n", (unsigned long long)badLength);
    for (int t = 0; t < 256; ++t)
      if (counts[t])
        std::printf("  %c %12llu\n", t, (unsigned long long)counts[t]);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "itch_stats: %s\n", e.what());
    return 1;
  }
}
