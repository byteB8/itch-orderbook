// Replays an ITCH 5.0 file into order books for every instrument, then prints the feed statistics and
// the top of one book.
//
//   replay <file> [--levels vector|map] [--index flat|std] [--symbol AAPL] [--depth 5]
//                 [--summary out.csv]
//
// --summary writes one row per instrument with orders on the book: the state that
// scripts/check_books.py computes independently.
#include "feed_book.h"
#include "itch_file.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace {

struct Options {
  std::string file, levels = "vector", index = "flat", symbol = "AAPL", summary;
  int depth = 5;
};

template <class Levels>
void printSide(const char *name, const Levels &levels, int depth) {
  std::printf("  %s:", name);
  levels.forEachFromBest([&](const Level &l) {
    if (depth-- > 0)
      std::printf("  %u @ %.4f (%u)", unsigned(l.shares), l.price / 10000.0, l.orders);
  });
  std::printf("\n");
}

template <class Feed>
int run(const Options &opt) {
  ItchFile file(opt.file);
  Feed feed;
  auto start = std::chrono::steady_clock::now();
  size_t n = file.forEach([&](const uint8_t *m, uint16_t) { feed.apply(m); });
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  const FeedStats &s = feed.stats();
  std::printf("%zu messages in %.2f s: %.1f ns per message (levels %s, index %s)\n", n, seconds, 1e9 * seconds / n,
              opt.levels.c_str(), opt.index.c_str());
  std::printf("adds %llu, executions %llu, cancels %llu, deletes %llu, replaces %llu, other %llu\n",
              (unsigned long long)s.adds, (unsigned long long)s.executions, (unsigned long long)s.cancels,
              (unsigned long long)s.deletes, (unsigned long long)s.replaces, (unsigned long long)s.other);
  std::printf("unknown orders %llu, over-executed %llu, orders on the book at the end %zu\n",
              (unsigned long long)s.unknownOrders, (unsigned long long)s.overExecuted, feed.liveOrders());

  int locate = feed.locateOf(opt.symbol);
  if (locate >= 0) {
    const auto &book = feed.book(uint16_t(locate));
    std::printf("%s (locate %d): %zu bid levels, %zu ask levels\n", opt.symbol.c_str(), locate, book.bids().size(),
                book.asks().size());
    printSide("bids", book.bids(), opt.depth);
    printSide("asks", book.asks(), opt.depth);
  }

  if (!opt.summary.empty()) {
    FILE *out = std::fopen(opt.summary.c_str(), "w");
    if (!out)
      throw std::runtime_error("cannot write " + opt.summary);
    std::fprintf(out, "locate,symbol,bid_levels,ask_levels,best_bid,best_ask,bid_shares,ask_shares,bid_orders,ask_orders\n");
    for (int i = 0; i < 65536; ++i) {
      const auto &book = feed.book(uint16_t(i));
      if (book.bids().size() + book.asks().size() == 0)
        continue;
      unsigned long long shares[2] = {0, 0}, orders[2] = {0, 0};
      book.bids().forEachFromBest([&](const Level &l) { shares[0] += l.shares; orders[0] += l.orders; });
      book.asks().forEachFromBest([&](const Level &l) { shares[1] += l.shares; orders[1] += l.orders; });
      std::fprintf(out, "%d,%s,%zu,%zu,%u,%u,%llu,%llu,%llu,%llu\n", i, feed.symbol(uint16_t(i)).c_str(),
                   book.bids().size(), book.asks().size(), book.bids().best() ? book.bids().best()->price : 0u,
                   book.asks().best() ? book.asks().best()->price : 0u, shares[0], shares[1], orders[0], orders[1]);
    }
    std::fclose(out);
  }
  return s.unknownOrders == 0 && s.overExecuted == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    auto value = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (!std::strcmp(argv[i], "--levels"))
      opt.levels = value();
    else if (!std::strcmp(argv[i], "--index"))
      opt.index = value();
    else if (!std::strcmp(argv[i], "--symbol"))
      opt.symbol = value();
    else if (!std::strcmp(argv[i], "--depth"))
      opt.depth = std::stoi(value());
    else if (!std::strcmp(argv[i], "--summary"))
      opt.summary = value();
    else
      opt.file = argv[i];
  }
  if (opt.file.empty()) {
    std::fprintf(stderr, "usage: replay <file> [--levels vector|map] [--index flat|std] [--symbol S] "
                         "[--depth N] [--summary out.csv]\n");
    return 2;
  }
  try {
    bool vec = opt.levels == "vector", flat = opt.index == "flat";
    if (vec && flat)
      return run<FeedBook<VectorLevels, FlatOrderIndex>>(opt);
    if (!vec && flat)
      return run<FeedBook<MapLevels, FlatOrderIndex>>(opt);
    if (vec)
      return run<FeedBook<VectorLevels, StdOrderIndex>>(opt);
    return run<FeedBook<MapLevels, StdOrderIndex>>(opt);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "replay: %s\n", e.what());
    return 1;
  }
}
