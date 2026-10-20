#include "check.h"
#include "spsc_queue.h"

#include <cstdint>
#include <memory>
#include <thread>

namespace {

// A value whose fields must agree, so a torn or stale read shows up.
struct Item {
  uint64_t seq;
  uint64_t triple;
  uint64_t check;
};
Item make(uint64_t seq) { return Item{seq, seq * 3, seq ^ 0x5bd1e995u}; }
bool valid(const Item &it, uint64_t seq) { return it.seq == seq && it.triple == seq * 3 && it.check == (seq ^ 0x5bd1e995u); }

void singleThread() {
  auto q = std::make_unique<SpscQueue<Item, 8>>();
  Item out{};
  CHECK(!q->tryPop(out));
  size_t wrong = 0;
  uint64_t pushed = 0, popped = 0;
  for (int round = 0; round < 1000; ++round) {  // fills and empties it many times, wrapping the indices
    while (q->tryPush(make(pushed)))
      ++pushed;
    wrong += pushed - popped != 8;  // full at exactly its capacity
    for (int k = 0; k < 5 && q->tryPop(out); ++k)
      wrong += !valid(out, popped++);
  }
  while (q->tryPop(out))
    wrong += !valid(out, popped++);
  CHECK_EQ(wrong, 0u);
  CHECK_EQ(popped, pushed);
}

// One producer and one consumer thread; every item must arrive once, in order, intact.
void twoThreads(uint64_t n) {
  auto q = std::make_unique<SpscQueue<Item, 1024>>();
  std::thread producer([&] {
    for (uint64_t i = 0; i < n; ++i)
      while (!q->tryPush(make(i))) {
      }
  });
  uint64_t wrong = 0;
  Item out{};
  for (uint64_t i = 0; i < n; ++i) {
    while (!q->tryPop(out)) {
    }
    wrong += !valid(out, i);
  }
  producer.join();
  CHECK_EQ(wrong, 0u);
  CHECK(!q->tryPop(out));
}

}  // namespace

int main(int argc, char **) {
  singleThread();
  twoThreads(argc > 1 ? 200000 : 10000000);  // any argument: a shorter run, for ThreadSanitizer
  return report("test_spsc_queue");
}
