#include "feed_book.h"

#include "itch.h"

template <template <bool> class Levels, class Index>
FeedBook<Levels, Index>::FeedBook(size_t expectedOrders) : books_(65536), symbols_(65536), index_(expectedOrders) {
  pool_.reserve(expectedOrders);
}

template <template <bool> class Levels, class Index>
void FeedBook<Levels, Index>::apply(const uint8_t *m) {
  ++stats_.messages;
  uint16_t locate = itch::be16(m + 1);
  switch (m[0]) {
    case 'A':
    case 'F': {
      itch::AddOrder a = itch::addOrder(m);
      int32_t idx = pool_.alloc(Order{a.ref, a.shares, a.price, -1, -1, locate, a.side});
      index_.insert(a.ref, uint32_t(idx));
      books_[locate].add(pool_, idx);
      ++stats_.adds;
      break;
    }
    case 'E':
    case 'C': {
      itch::OrderExecuted e = itch::orderExecuted(m);
      reduce(e.ref, e.shares);
      ++stats_.executions;
      break;
    }
    case 'X': {
      itch::OrderCancel x = itch::orderCancel(m);
      reduce(x.ref, x.shares);
      ++stats_.cancels;
      break;
    }
    case 'D': {
      uint64_t ref = itch::orderDelete(m);
      if (uint32_t *slot = index_.find(ref))
        remove(ref, int32_t(*slot));
      else
        ++stats_.unknownOrders;
      ++stats_.deletes;
      break;
    }
    case 'U': {
      itch::OrderReplace u = itch::orderReplace(m);
      ++stats_.replaces;
      uint32_t *slot = index_.find(u.oldRef);
      if (!slot) {
        ++stats_.unknownOrders;
        break;
      }
      Order old = pool_[int32_t(*slot)];  // a copy: remove() frees the slot
      remove(u.oldRef, int32_t(*slot));
      int32_t idx = pool_.alloc(Order{u.newRef, u.shares, u.price, -1, -1, old.book, old.side});
      index_.insert(u.newRef, uint32_t(idx));
      books_[old.book].add(pool_, idx);
      break;
    }
    case 'R':
      symbols_[locate] = std::string(itch::stockSymbol(m));
      ++stats_.other;
      break;
    default:
      ++stats_.other;
  }
}

template <template <bool> class Levels, class Index>
void FeedBook<Levels, Index>::reduce(uint64_t ref, uint32_t shares) {
  uint32_t *slot = index_.find(ref);
  if (!slot) {
    ++stats_.unknownOrders;
    return;
  }
  int32_t idx = int32_t(*slot);
  Order &o = pool_[idx];
  if (shares < o.shares) {
    books_[o.book].reduce(pool_, idx, shares);
  } else {
    stats_.overExecuted += shares > o.shares;
    remove(ref, idx);
  }
}

template <template <bool> class Levels, class Index>
void FeedBook<Levels, Index>::remove(uint64_t ref, int32_t idx) {
  books_[pool_[idx].book].remove(pool_, idx);
  index_.erase(ref);
  pool_.release(idx);
}

template <template <bool> class Levels, class Index>
int FeedBook<Levels, Index>::locateOf(std::string_view symbol) const {
  for (size_t i = 0; i < symbols_.size(); ++i)
    if (symbols_[i] == symbol)
      return int(i);
  return -1;
}

template class FeedBook<VectorLevels, FlatOrderIndex>;
template class FeedBook<MapLevels, FlatOrderIndex>;
template class FeedBook<VectorLevels, StdOrderIndex>;
template class FeedBook<MapLevels, StdOrderIndex>;
