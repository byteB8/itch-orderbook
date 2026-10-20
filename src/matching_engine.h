#ifndef MATCHING_ENGINE_H
#define MATCHING_ENGINE_H

// A matching engine for one instrument, as an exchange runs it. An incoming order trades against the
// resting orders on the other side in price-time priority: the best price first and, at one price, the
// oldest order first. Each trade prints at the resting order's price. What is left of a limit order then
// rests on the book; an immediate-or-cancel order drops it instead.

#include "order_book.h"
#include "order_index.h"

#include <algorithm>
#include <cstdint>

struct Trade {
  uint64_t resting;   // reference of the order that was on the book
  uint64_t incoming;  // reference of the order that arrived
  uint32_t price;
  uint32_t shares;
  bool operator==(const Trade &) const = default;
};

enum class TimeInForce { Day, ImmediateOrCancel };

template <template <bool> class Levels = VectorLevels>
class MatchingEngine {
 public:
  explicit MatchingEngine(size_t expectedOrders = 1 << 16) : index_(expectedOrders) { pool_.reserve(expectedOrders); }

  // Calls onTrade(const Trade &) for each fill, in order. Returns the shares left on the book.
  // References must be unique among the orders on the book.
  template <class F>
  uint32_t submit(uint64_t ref, char side, uint32_t price, uint32_t shares, F onTrade,
                  TimeInForce tif = TimeInForce::Day) {
    if (side == 'B')
      match<true>(book_.asks(), ref, price, shares, onTrade);
    else
      match<false>(book_.bids(), ref, price, shares, onTrade);
    if (shares == 0 || tif == TimeInForce::ImmediateOrCancel)
      return 0;
    int32_t idx = pool_.alloc(Order{ref, shares, price, -1, -1, 0, side});
    index_.insert(ref, uint32_t(idx));
    book_.add(pool_, idx);
    return shares;
  }

  // Removes a resting order; false if it is not on the book.
  bool cancel(uint64_t ref) {
    uint32_t *slot = index_.find(ref);
    if (!slot)
      return false;
    int32_t idx = int32_t(*slot);
    book_.remove(pool_, idx);
    index_.erase(ref);
    pool_.release(idx);
    return true;
  }

  const Book<Levels> &book() const { return book_; }
  const OrderPool &pool() const { return pool_; }

 private:
  // Buying (Buy = true) takes from the asks while the best ask is at or below the limit price; selling
  // takes from the bids while the best bid is at or above it.
  template <bool Buy, class Side, class F>
  void match(Side &opposite, uint64_t ref, uint32_t limit, uint32_t &shares, F &onTrade) {
    while (shares > 0) {
      const Level *best = opposite.best();
      if (!best || (Buy ? best->price > limit : best->price < limit))
        return;
      int32_t idx = best->head;
      Order &resting = pool_[idx];
      uint32_t traded = std::min(shares, resting.shares);
      onTrade(Trade{resting.ref, ref, resting.price, traded});
      shares -= traded;
      if (traded < resting.shares) {
        book_.reduce(pool_, idx, traded);
      } else {
        uint64_t restingRef = resting.ref;
        book_.remove(pool_, idx);  // may erase the level: best is fetched again next time round
        index_.erase(restingRef);
        pool_.release(idx);
      }
    }
  }

  Book<Levels> book_;
  OrderPool pool_;
  FlatOrderIndex index_;
};

#endif
