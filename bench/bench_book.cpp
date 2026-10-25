// Book-building benchmark on a real ITCH file.
//
//   bench_book <itch file> <out_dir> [--rounds 5]
//
// 1. Throughput: replays the whole file into fresh books with each combination of price-level container
//    (sorted vector, std::map) and order index (open addressing, std::unordered_map), and once more with
//    the default books on 4 KB pages instead of huge pages. The variants run in rotating order within each
//    round, so a slow spell of the machine hits all of them; the median of the rounds is reported. Writes
//    book.csv.
// 2. Latency: one more replay with the default books, timing each message with the TSC; percentiles per
//    message type. Writes book_latency.csv.
#include "cli.h"
#include "cpu.h"
#include "feed_book.h"
#include "huge_pages.h"
#include "itch_file.h"
#include "latency.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

template <class Feed>
double nsPerMessage(const ItchFile &file, bool hugePages = true) {
  huge_pages::enabled() = hugePages;  // for every allocation of this replay, growth included
  auto feed = std::make_unique<Feed>();
  auto start = std::chrono::steady_clock::now();
  size_t n = file.forEach([&](const uint8_t *m, uint16_t) { feed->apply(m); });
  double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
  huge_pages::enabled() = true;
  if (feed->stats().unknownOrders != 0)
    throw std::runtime_error("books out of step with the feed");
  return ns / double(n);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: bench_book <itch file> <out_dir> [--rounds N] [--cpu N]\n");
    return 2;
  }
  int rounds = 5, cpu = -1;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--rounds"))
      rounds = optionValue<int>("bench_book", argv[i], argv[i + 1]);
    else if (!std::strcmp(argv[i], "--cpu"))
      cpu = optionValue<int>("bench_book", argv[i], argv[i + 1]);
  }
  try {
    pinThisThread(cpu);
    ItchFile file(argv[1]);
    std::string out = argv[2];
    file.forEach([](const uint8_t *, uint16_t) {});  // read the file into the page cache first

    struct Variant {
      const char *levels, *index;
      std::function<double()> run;
      std::vector<double> ns;
    };
    std::vector<Variant> variants = {
        {"vector", "flat", [&] { return nsPerMessage<FeedBook<VectorLevels, FlatOrderIndex>>(file); }, {}},
        {"vector", "std", [&] { return nsPerMessage<FeedBook<VectorLevels, StdOrderIndex>>(file); }, {}},
        {"map", "flat", [&] { return nsPerMessage<FeedBook<MapLevels, FlatOrderIndex>>(file); }, {}},
        {"map", "std", [&] { return nsPerMessage<FeedBook<MapLevels, StdOrderIndex>>(file); }, {}},
        {"vector", "flat-4k", [&] { return nsPerMessage<FeedBook<VectorLevels, FlatOrderIndex>>(file, false); }, {}},
    };
    FILE *csv = std::fopen((out + "/book.csv").c_str(), "w");
    if (!csv)
      throw std::runtime_error("cannot write to " + out);
    std::fprintf(csv, "levels,index,round,ns_per_message\n");
    for (int r = 0; r < rounds; ++r)
      for (size_t k = 0; k < variants.size(); ++k) {
        Variant &v = variants[(r + k) % variants.size()];
        double ns = v.run();
        v.ns.push_back(ns);
        std::fprintf(csv, "%s,%s,%d,%.2f\n", v.levels, v.index, r, ns);
        std::printf("round %d: levels %-6s index %-7s %.1f ns per message\n", r, v.levels, v.index, ns);
        std::fflush(stdout);
      }
    std::fclose(csv);
    for (Variant &v : variants) {
      std::sort(v.ns.begin(), v.ns.end());
      std::printf("median: levels %-6s index %-7s %.1f ns per message (min %.1f, max %.1f)\n", v.levels, v.index,
                  v.ns[v.ns.size() / 2], v.ns.front(), v.ns.back());
    }

    TscClock clock;
    auto feed = std::make_unique<DefaultFeedBook>();
    const std::string types = "AFECXDU";              // the messages that change the book
    std::vector<LatencyHistogram> byType(types.size() + 1);  // the last one: every other type
    LatencyHistogram all;
    file.forEach([&](const uint8_t *m, uint16_t) {
      uint64_t t0 = TscClock::now();
      feed->apply(m);
      uint64_t ns = clock.toNs(TscClock::now() - t0);
      all.add(ns);
      size_t k = types.find(char(m[0]));
      byType[k == std::string::npos ? types.size() : k].add(ns);
    });
    FILE *lat = std::fopen((out + "/book_latency.csv").c_str(), "w");
    std::fprintf(lat, "type,messages,p50_ns,p90_ns,p99_ns,p999_ns,max_ns\n");
    auto row = [&](const std::string &type, const LatencyHistogram &h) {
      std::fprintf(lat, "%s,%llu,%llu,%llu,%llu,%llu,%llu\n", type.c_str(), (unsigned long long)h.count(),
                   (unsigned long long)h.percentile(50), (unsigned long long)h.percentile(90),
                   (unsigned long long)h.percentile(99), (unsigned long long)h.percentile(99.9),
                   (unsigned long long)h.max());
      std::printf("%-4s %10llu messages: p50 %llu ns, p99 %llu ns, p99.9 %llu ns\n", type.c_str(),
                  (unsigned long long)h.count(), (unsigned long long)h.percentile(50),
                  (unsigned long long)h.percentile(99), (unsigned long long)h.percentile(99.9));
    };
    row("all", all);
    for (size_t k = 0; k <= types.size(); ++k)
      row(k < types.size() ? std::string(1, types[k]) : std::string("other"), byType[k]);
    std::fclose(lat);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "bench_book: %s\n", e.what());
    return 1;
  }
}
