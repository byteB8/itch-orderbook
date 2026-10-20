#include "check.h"
#include "order_index.h"

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace {

// Random inserts, erases and lookups, checked against std::unordered_map after every operation.
// Three quarters of the erases and lookups pick a key that is present. With few live keys in a small
// table (maxLive), probe runs often wrap around the end of the array and get erased from the middle,
// which is where backward-shift deletion can go wrong.
template <class Index>
void againstStdMap(uint64_t keyRange, size_t expected, int ops, uint32_t seed, size_t maxLive = SIZE_MAX) {
  Index index(expected);
  std::unordered_map<uint64_t, uint32_t> ref;
  std::vector<uint64_t> live;                 // the keys in ref, to pick from
  std::unordered_map<uint64_t, size_t> where;  // key -> position in live
  std::mt19937_64 rng(seed);
  auto pick = [&] { return !live.empty() && rng() % 4 ? live[rng() % live.size()] : rng() % keyRange; };
  size_t wrong = 0, erased = 0;
  for (int op = 0; op < ops; ++op) {
    switch (rng() % 3) {
      case 0: {
        uint64_t key = rng() % keyRange;
        if (!ref.count(key) && ref.size() < maxLive) {
          uint32_t value = uint32_t(rng());
          index.insert(key, value);
          ref[key] = value;
          where[key] = live.size();
          live.push_back(key);
        }
        break;
      }
      case 1: {
        uint64_t key = pick();
        bool present = ref.erase(key) == 1;
        wrong += index.erase(key) != present;
        if (present) {
          ++erased;
          size_t i = where[key];
          live[i] = live.back();
          where[live[i]] = i;
          live.pop_back();
          where.erase(key);
        }
        break;
      }
      default: {
        uint64_t key = pick();
        uint32_t *v = index.find(key);
        auto it = ref.find(key);
        wrong += (v == nullptr) != (it == ref.end()) || (v && *v != it->second);
      }
    }
    wrong += index.size() != ref.size();
  }
  for (const auto &[key, value] : ref) {
    uint32_t *v = index.find(key);
    wrong += !v || *v != value;
  }
  CHECK_EQ(wrong, 0u);
  CHECK(erased > size_t(ops) / 8);  // the run really exercised deletion
}

}  // namespace

int main() {
  againstStdMap<FlatOrderIndex>(1ull << 32, 8, 1000000, 5, 8);  // 16 slots, at most 8 used: runs wrap often
  againstStdMap<FlatOrderIndex>(64, 4, 200000, 1);
  againstStdMap<FlatOrderIndex>(5000, 16, 1000000, 2);
  againstStdMap<FlatOrderIndex>(1ull << 40, 1 << 16, 1000000, 3);
  againstStdMap<StdOrderIndex>(5000, 16, 200000, 4);

  // consecutive keys, like ITCH order reference numbers
  FlatOrderIndex seq(8);
  for (uint64_t k = 1; k <= 100000; ++k)
    seq.insert(k, uint32_t(k * 3));
  size_t wrong = 0;
  for (uint64_t k = 1; k <= 100000; k += 2)
    wrong += !seq.erase(k);
  for (uint64_t k = 1; k <= 100000; ++k) {
    uint32_t *v = seq.find(k);
    wrong += k % 2 ? v != nullptr : (!v || *v != k * 3);
  }
  CHECK_EQ(wrong, 0u);
  CHECK_EQ(seq.size(), 50000u);
  CHECK(seq.capacity() >= 2 * 50000);

  return report("test_order_index");
}
