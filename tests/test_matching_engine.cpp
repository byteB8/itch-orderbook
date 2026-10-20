#include "check.h"
#include "matching_engine.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <tuple>
#include <vector>

namespace {

// The obvious implementation: a list of resting orders, searched for the best one at every fill.
struct ReferenceEngine {
  struct Resting {
    uint64_t ref;
    char side;
    uint32_t price;
    uint32_t shares;
    uint64_t seq;
  };
  std::vector<Resting> book;
  uint64_t seq = 0;

  std::vector<Trade> submit(uint64_t ref, char side, uint32_t price, uint32_t shares, bool rest) {
    std::vector<Trade> trades;
    while (shares > 0) {
      auto best = book.end();
      for (auto it = book.begin(); it != book.end(); ++it) {
        if (it->side == side)
          continue;
        bool crosses = side == 'B' ? it->price <= price : it->price >= price;
        if (!crosses)
          continue;
        if (best == book.end())
          best = it;
        else if (it->price != best->price ? (side == 'B' ? it->price < best->price : it->price > best->price)
                                         : it->seq < best->seq)
          best = it;
      }
      if (best == book.end())
        break;
      uint32_t traded = std::min(shares, best->shares);
      trades.push_back(Trade{best->ref, ref, best->price, traded});
      shares -= traded;
      best->shares -= traded;
      if (best->shares == 0)
        book.erase(best);
    }
    if (shares > 0 && rest)
      book.push_back(Resting{ref, side, price, shares, seq++});
    return trades;
  }
  bool cancel(uint64_t ref) {
    auto it = std::find_if(book.begin(), book.end(), [&](const Resting &r) { return r.ref == ref; });
    if (it == book.end())
      return false;
    book.erase(it);
    return true;
  }
  // Resting orders of one side in priority order: (ref, price, shares).
  std::vector<std::tuple<uint64_t, uint32_t, uint32_t>> queue(char side) const {
    std::vector<Resting> mine;
    for (const Resting &r : book)
      if (r.side == side)
        mine.push_back(r);
    std::sort(mine.begin(), mine.end(), [&](const Resting &a, const Resting &b) {
      if (a.price != b.price)
        return side == 'B' ? a.price > b.price : a.price < b.price;
      return a.seq < b.seq;
    });
    std::vector<std::tuple<uint64_t, uint32_t, uint32_t>> out;
    for (const Resting &r : mine)
      out.emplace_back(r.ref, r.price, r.shares);
    return out;
  }
};

template <class Engine>
std::vector<std::tuple<uint64_t, uint32_t, uint32_t>> queue(const Engine &engine, char side) {
  std::vector<std::tuple<uint64_t, uint32_t, uint32_t>> out;
  auto walk = [&](const auto &levels) {
    levels.forEachFromBest([&](const Level &l) {
      for (int32_t i = l.head; i >= 0; i = engine.pool()[i].next)
        out.emplace_back(engine.pool()[i].ref, engine.pool()[i].price, engine.pool()[i].shares);
    });
  };
  if (side == 'B')
    walk(engine.book().bids());
  else
    walk(engine.book().asks());
  return out;
}

template <class Engine>
std::vector<Trade> submit(Engine &engine, uint64_t ref, char side, uint32_t price, uint32_t shares,
                          TimeInForce tif = TimeInForce::Day) {
  std::vector<Trade> trades;
  engine.submit(ref, side, price, shares, [&](const Trade &t) { trades.push_back(t); }, tif);
  return trades;
}

template <template <bool> class Levels>
void fixedCases() {
  MatchingEngine<Levels> e;
  submit(e, 1, 'S', 101, 10);
  submit(e, 2, 'S', 101, 20);  // behind order 1 at 101
  submit(e, 3, 'S', 100, 5);   // the best ask
  submit(e, 4, 'B', 98, 7);
  // a buy for 30 up to 101: 5 @ 100 from order 3, then 10 @ 101 from order 1 (older), then 15 from order 2
  auto t = submit(e, 5, 'B', 101, 30);
  CHECK((t == std::vector<Trade>{{3, 5, 100, 5}, {1, 5, 101, 10}, {2, 5, 101, 15}}));
  CHECK((queue(e, 'S') == std::vector<std::tuple<uint64_t, uint32_t, uint32_t>>{{2, 101, 5}}));

  // a sell below the best bid trades at the bid's price; the rest of an IOC order is dropped
  t = submit(e, 6, 'S', 90, 10, TimeInForce::ImmediateOrCancel);
  CHECK((t == std::vector<Trade>{{4, 6, 98, 7}}));
  CHECK(queue(e, 'B').empty());
  CHECK((queue(e, 'S') == std::vector<std::tuple<uint64_t, uint32_t, uint32_t>>{{2, 101, 5}}));

  // no cross: the order rests; cancel removes it
  CHECK(submit(e, 7, 'B', 100, 3).empty());
  CHECK(e.cancel(7));
  CHECK(!e.cancel(7));
  CHECK(queue(e, 'B').empty());
}

// Random orders against the reference: every trade, in order, and both sides of the book.
template <template <bool> class Levels>
void randomOrders() {
  std::mt19937 rng(7);
  MatchingEngine<Levels> engine;
  ReferenceEngine ref;
  std::vector<uint64_t> resting;
  size_t wrongTrades = 0, wrongBooks = 0, trades = 0;
  for (uint64_t id = 1; id <= 100000; ++id) {
    if (!resting.empty() && rng() % 5 == 0) {
      size_t pick = rng() % resting.size();
      bool a = engine.cancel(resting[pick]), b = ref.cancel(resting[pick]);
      wrongTrades += a != b;
      resting[pick] = resting.back();
      resting.pop_back();
      continue;
    }
    char side = rng() % 2 ? 'B' : 'S';
    uint32_t price = 1000 + uint32_t(rng() % 21) - 10;  // crosses often
    uint32_t shares = 1 + rng() % 100;
    bool ioc = rng() % 10 == 0;
    auto got = submit(engine, id, side, price, shares, ioc ? TimeInForce::ImmediateOrCancel : TimeInForce::Day);
    auto want = ref.submit(id, side, price, shares, !ioc);
    wrongTrades += got != want;
    trades += got.size();
    if (!ioc)
      resting.push_back(id);  // may have been filled; cancel then returns false in both
    if (id % 50 == 0)
      wrongBooks += queue(engine, 'B') != ref.queue('B') || queue(engine, 'S') != ref.queue('S');
  }
  CHECK_EQ(wrongTrades, 0u);
  CHECK_EQ(wrongBooks, 0u);
  CHECK(trades > 50000);
}

}  // namespace

int main() {
  fixedCases<VectorLevels>();
  fixedCases<MapLevels>();
  randomOrders<VectorLevels>();
  randomOrders<MapLevels>();
  return report("test_matching_engine");
}
