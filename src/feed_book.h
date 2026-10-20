#ifndef FEED_BOOK_H
#define FEED_BOOK_H

// Order books for every instrument of an ITCH 5.0 feed, kept up to date message by message.

#include "order_book.h"
#include "order_index.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct FeedStats {
  uint64_t messages = 0;
  uint64_t adds = 0;
  uint64_t executions = 0;
  uint64_t cancels = 0;
  uint64_t deletes = 0;
  uint64_t replaces = 0;
  uint64_t other = 0;          // messages that do not change the visible book
  uint64_t unknownOrders = 0;  // an execute, cancel, delete or replace of an order that is not on the book
  uint64_t overExecuted = 0;   // more shares executed or cancelled than the order had
};

// Levels: MapLevels or VectorLevels. Index: FlatOrderIndex or StdOrderIndex.
template <template <bool> class Levels, class Index>
class FeedBook {
 public:
  explicit FeedBook(size_t expectedOrders = size_t(1) << 22);

  // Applies one message (starting at its type byte). Messages must come in feed order.
  void apply(const uint8_t *message);

  // Book of an instrument by ITCH stock locate. Books never move, so the reference stays valid.
  const Book<Levels> &book(uint16_t locate) const { return books_[locate]; }
  const std::string &symbol(uint16_t locate) const { return symbols_[locate]; }
  int locateOf(std::string_view symbol) const;  // -1 if no stock directory message named it
  const FeedStats &stats() const { return stats_; }
  size_t liveOrders() const { return pool_.live(); }
  const OrderPool &pool() const { return pool_; }

 private:
  void reduce(uint64_t ref, uint32_t shares);
  void remove(uint64_t ref, int32_t idx);

  std::vector<Book<Levels>> books_;    // one per possible stock locate (65,536), never resized
  std::vector<std::string> symbols_;
  OrderPool pool_;
  Index index_;                        // order reference number -> pool slot
  FeedStats stats_;
};

using DefaultFeedBook = FeedBook<VectorLevels, FlatOrderIndex>;

#endif
