#include "check.h"
#include "feed_book.h"
#include "itch_writer.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

namespace {

// A level as the tests compare it: price, shares, order count and the queue in time priority.
struct LevelView {
  uint32_t price;
  uint64_t shares;
  uint32_t orders;
  std::vector<uint64_t> queue;
  bool operator==(const LevelView &) const = default;
};

template <class Feed>
std::vector<LevelView> depth(const Feed &feed, uint16_t locate, char side) {
  std::vector<LevelView> out;
  auto collect = [&](const auto &levels) {
    levels.forEachFromBest([&](const Level &l) {
      LevelView v{l.price, l.shares, l.orders, {}};
      for (int32_t i = l.head; i >= 0; i = feed.pool()[i].next)
        v.queue.push_back(feed.pool()[i].ref);
      out.push_back(v);
    });
  };
  if (side == 'B')
    collect(feed.book(locate).bids());
  else
    collect(feed.book(locate).asks());
  return out;
}

// The obvious implementation: a map of orders; levels are computed by grouping and sorting.
struct ReferenceBook {
  struct Ord {
    uint16_t locate;
    char side;
    uint32_t price;
    uint32_t shares;
    uint64_t seq;  // arrival order: time priority
  };
  std::map<uint64_t, Ord> orders;
  uint64_t seq = 0;

  void add(uint16_t locate, uint64_t ref, char side, uint32_t shares, uint32_t price) {
    orders[ref] = Ord{locate, side, price, shares, seq++};
  }
  void reduce(uint64_t ref, uint32_t shares) {
    Ord &o = orders.at(ref);
    if (shares >= o.shares)
      orders.erase(ref);
    else
      o.shares -= shares;
  }
  void replace(uint64_t oldRef, uint64_t newRef, uint32_t shares, uint32_t price) {
    Ord o = orders.at(oldRef);
    orders.erase(oldRef);
    add(o.locate, newRef, o.side, shares, price);
  }
  std::vector<LevelView> depth(uint16_t locate, char side) const {
    std::vector<std::pair<const uint64_t *, const Ord *>> mine;
    for (const auto &[ref, o] : orders)
      if (o.locate == locate && o.side == side)
        mine.push_back({&ref, &o});
    std::sort(mine.begin(), mine.end(), [&](auto a, auto b) {
      if (a.second->price != b.second->price)
        return side == 'B' ? a.second->price > b.second->price : a.second->price < b.second->price;
      return a.second->seq < b.second->seq;
    });
    std::vector<LevelView> out;
    for (auto [ref, o] : mine) {
      if (out.empty() || out.back().price != o->price)
        out.push_back(LevelView{o->price, 0, 0, {}});
      out.back().shares += o->shares;
      ++out.back().orders;
      out.back().queue.push_back(*ref);
    }
    return out;
  }
};

template <class Feed>
void send(Feed &feed, const std::vector<uint8_t> &msg) {
  feed.apply(msg.data());
}

// A small book with known answers.
template <class Feed>
void fixedCases() {
  Feed feed(16);
  send(feed, addMsg(7, 1, 'B', 100, 100000));  // 100 @ 10.00
  send(feed, addMsg(7, 2, 'B', 50, 100100));   // 50 @ 10.01
  send(feed, addMsg(7, 3, 'B', 70, 100000));   // behind order 1 at 10.00
  send(feed, addMsg(7, 4, 'S', 30, 100500));
  send(feed, addMsg(7, 5, 'S', 20, 100300));
  CHECK((depth(feed, 7, 'B') == std::vector<LevelView>{{100100, 50, 1, {2}}, {100000, 170, 2, {1, 3}}}));
  CHECK((depth(feed, 7, 'S') == std::vector<LevelView>{{100300, 20, 1, {5}}, {100500, 30, 1, {4}}}));

  send(feed, executeMsg(7, 1, 40));  // order 1 keeps its place with 60 shares
  send(feed, cancelMsg(7, 3, 70));   // all of order 3
  send(feed, deleteMsg(7, 2));
  CHECK((depth(feed, 7, 'B') == std::vector<LevelView>{{100000, 60, 1, {1}}}));

  send(feed, replaceMsg(7, 5, 6, 25, 100200));  // ask 5 becomes 6: 25 @ 10.02
  CHECK((depth(feed, 7, 'S') == std::vector<LevelView>{{100200, 25, 1, {6}}, {100500, 30, 1, {4}}}));

  send(feed, deleteMsg(7, 999));      // not on the book
  send(feed, executeMsg(7, 4, 31));   // one share more than it has
  CHECK_EQ(feed.stats().unknownOrders, 1u);
  CHECK_EQ(feed.stats().overExecuted, 1u);
  CHECK((depth(feed, 7, 'S') == std::vector<LevelView>{{100200, 25, 1, {6}}}));
  CHECK_EQ(feed.liveOrders(), 2u);
}

// Random valid feeds on three instruments, compared with the reference after every 20 messages: every
// level of every side, including the order of each queue. Prices cluster near a mid price, with a
// tail far from it, as in real books.
void randomFeeds() {
  std::mt19937 rng(20261020);
  DefaultFeedBook vec(64);
  FeedBook<MapLevels, StdOrderIndex> map(64);
  ReferenceBook ref;
  std::vector<uint64_t> live;
  uint64_t nextRef = 1;
  size_t compared = 0, wrong = 0;
  auto price = [&](char side) {
    int ticks = int(rng() % 8 == 0 ? rng() % 400 : rng() % 12);
    return uint32_t(1000000 + (side == 'B' ? -ticks : ticks) * 100);
  };
  auto both = [&](const std::vector<uint8_t> &m) {
    vec.apply(m.data());
    map.apply(m.data());
  };
  for (int op = 0; op < 200000; ++op) {
    unsigned r = rng() % 100;
    if (live.empty() || (r < 36 && live.size() < 3000)) {
      uint16_t locate = uint16_t(1 + rng() % 3);
      char side = rng() % 2 ? 'B' : 'S';
      uint32_t shares = 1 + rng() % 500, p = price(side);
      both(addMsg(locate, nextRef, side, shares, p));
      ref.add(locate, nextRef, side, shares, p);
      live.push_back(nextRef++);
    } else {
      size_t pick = rng() % live.size();
      uint64_t id = live[pick];
      const auto &o = ref.orders.at(id);
      uint16_t locate = o.locate;
      bool gone = false;
      if (r < 66) {
        both(deleteMsg(locate, id));
        ref.orders.erase(id);
        gone = true;
      } else if (r < 86) {
        uint32_t shares = 1 + rng() % o.shares;  // up to all of it
        gone = shares == o.shares;
        both(r < 76 ? executeMsg(locate, id, shares) : cancelMsg(locate, id, shares));
        ref.reduce(id, shares);
      } else {
        uint32_t shares = 1 + rng() % 500, p = price(o.side);
        both(replaceMsg(locate, id, nextRef, shares, p));
        ref.replace(id, nextRef, shares, p);
        live[pick] = nextRef++;
      }
      if (gone) {
        live[pick] = live.back();
        live.pop_back();
      }
    }
    if (op % 20 == 0)
      for (uint16_t l = 1; l <= 3; ++l)
        for (char side : {'B', 'S'}) {
          auto want = ref.depth(l, side);
          wrong += depth(vec, l, side) != want;
          wrong += depth(map, l, side) != want;
          ++compared;
        }
  }
  CHECK_EQ(wrong, 0u);
  CHECK_EQ(compared, 60000u);
  CHECK_EQ(vec.stats().unknownOrders + map.stats().unknownOrders, 0u);
  CHECK_EQ(vec.liveOrders(), ref.orders.size());
}

}  // namespace

int main() {
  fixedCases<DefaultFeedBook>();
  fixedCases<FeedBook<MapLevels, FlatOrderIndex>>();
  fixedCases<FeedBook<VectorLevels, StdOrderIndex>>();
  fixedCases<FeedBook<MapLevels, StdOrderIndex>>();
  randomFeeds();
  return report("test_order_book");
}
