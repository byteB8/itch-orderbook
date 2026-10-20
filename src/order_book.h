#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

// The order book of one instrument: price levels on each side, and in each level its orders in time
// priority (FIFO). Orders live in an OrderPool and are linked by pool index, so adding, removing and
// shrinking an order needs no allocation once the pool and the levels have grown.

#include <cstdint>
#include <functional>
#include <map>
#include <type_traits>
#include <vector>

struct Order {
  uint64_t ref = 0;     // order reference number
  uint32_t shares = 0;  // shares still on the book
  uint32_t price = 0;   // four implied decimal places
  int32_t prev = -1;    // the next older order at this price, -1 if none
  int32_t next = -1;    // the next newer order at this price, -1 if none
  uint16_t book = 0;    // instrument (ITCH stock locate)
  char side = 'B';      // 'B' buy, 'S' sell
};

// Every order of a feed in one vector; the slots of removed orders are reused.
class OrderPool {
 public:
  int32_t alloc(const Order &o) {
    if (free_.empty()) {
      orders_.push_back(o);
      return int32_t(orders_.size() - 1);
    }
    int32_t i = free_.back();
    free_.pop_back();
    orders_[size_t(i)] = o;
    return i;
  }
  void release(int32_t i) { free_.push_back(i); }
  Order &operator[](int32_t i) { return orders_[size_t(i)]; }
  const Order &operator[](int32_t i) const { return orders_[size_t(i)]; }
  size_t live() const { return orders_.size() - free_.size(); }
  void reserve(size_t n) { orders_.reserve(n); }

 private:
  std::vector<Order> orders_;
  std::vector<int32_t> free_;
};

// One price on one side.
struct Level {
  uint32_t price = 0;
  uint32_t orders = 0;
  uint64_t shares = 0;  // visible shares of all its orders
  int32_t head = -1;    // oldest order: first in line to trade
  int32_t tail = -1;    // newest order
};

// Bids are better at higher prices, asks at lower ones.
template <bool Bid>
constexpr bool better(uint32_t a, uint32_t b) {
  return Bid ? a > b : a < b;
}

// The levels of one side in a std::map, best first: O(log n) per lookup, one heap node per level.
template <bool Bid>
class MapLevels {
 public:
  Level *find(uint32_t price) {
    auto it = map_.find(price);
    return it == map_.end() ? nullptr : &it->second;
  }
  Level &findOrInsert(uint32_t price) { return map_.try_emplace(price, Level{price}).first->second; }
  void erase(uint32_t price) { map_.erase(price); }
  const Level *best() const { return map_.empty() ? nullptr : &map_.begin()->second; }
  size_t size() const { return map_.size(); }
  template <class F>
  void forEachFromBest(F f) const {
    for (const auto &[price, level] : map_)
      f(level);
  }

 private:
  using Compare = std::conditional_t<Bid, std::greater<uint32_t>, std::less<uint32_t>>;
  std::map<uint32_t, Level, Compare> map_;
};

// The levels of one side in a vector sorted from worst to best, so the best level is at the back.
// Most changes happen near the top of the book, so lookups scan from the back, and inserting or
// erasing a level near the top moves only the few levels behind it. Any insert or erase invalidates
// pointers to levels.
template <bool Bid>
class VectorLevels {
 public:
  Level *find(uint32_t price) {
    for (size_t i = levels_.size(); i-- > 0;) {
      uint32_t p = levels_[i].price;
      if (p == price)
        return &levels_[i];
      if (better<Bid>(price, p))  // passed the place where price would be
        return nullptr;
    }
    return nullptr;
  }
  Level &findOrInsert(uint32_t price) {
    size_t i = levels_.size();
    while (i > 0 && better<Bid>(levels_[i - 1].price, price))
      --i;
    if (i > 0 && levels_[i - 1].price == price)
      return levels_[i - 1];
    return *levels_.insert(levels_.begin() + std::ptrdiff_t(i), Level{price});
  }
  void erase(uint32_t price) {
    for (size_t i = levels_.size(); i-- > 0;)
      if (levels_[i].price == price) {
        levels_.erase(levels_.begin() + std::ptrdiff_t(i));
        return;
      }
  }
  const Level *best() const { return levels_.empty() ? nullptr : &levels_.back(); }
  size_t size() const { return levels_.size(); }
  template <class F>
  void forEachFromBest(F f) const {
    for (size_t i = levels_.size(); i-- > 0;)
      f(levels_[i]);
  }

 private:
  std::vector<Level> levels_;
};

template <template <bool> class Levels>
class Book {
 public:
  // Puts the order at the back of its price level's queue.
  void add(OrderPool &pool, int32_t idx) {
    Order &o = pool[idx];
    onSide(o.side, [&](auto &levels) {
      Level &l = levels.findOrInsert(o.price);
      o.prev = l.tail;
      o.next = -1;
      if (l.tail >= 0)
        pool[l.tail].next = idx;
      else
        l.head = idx;
      l.tail = idx;
      ++l.orders;
      l.shares += o.shares;
    });
  }

  // Takes the order out of its queue, and the level out of the book if it is now empty.
  void remove(OrderPool &pool, int32_t idx) {
    const Order &o = pool[idx];
    onSide(o.side, [&](auto &levels) {
      Level *l = levels.find(o.price);
      if (o.prev >= 0)
        pool[o.prev].next = o.next;
      else
        l->head = o.next;
      if (o.next >= 0)
        pool[o.next].prev = o.prev;
      else
        l->tail = o.prev;
      --l->orders;
      l->shares -= o.shares;
      if (l->orders == 0)
        levels.erase(o.price);
    });
  }

  // Removes some of the order's shares; it keeps its place in the queue. Needs shares < its shares.
  void reduce(OrderPool &pool, int32_t idx, uint32_t shares) {
    Order &o = pool[idx];
    onSide(o.side, [&](auto &levels) { levels.find(o.price)->shares -= shares; });
    o.shares -= shares;
  }

  Levels<true> &bids() { return bids_; }
  Levels<false> &asks() { return asks_; }
  const Levels<true> &bids() const { return bids_; }
  const Levels<false> &asks() const { return asks_; }

 private:
  template <class F>
  void onSide(char side, F f) {
    if (side == 'B')
      f(bids_);
    else
      f(asks_);
  }

  Levels<true> bids_;
  Levels<false> asks_;
};

#endif
