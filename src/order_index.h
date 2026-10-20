#ifndef ORDER_INDEX_H
#define ORDER_INDEX_H

// Maps an order reference number to the order's slot in the order pool. Two versions with the same
// interface: find returns nullptr if the key is absent; insert requires an absent key; erase returns
// whether the key was there.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Open addressing with linear probing: 16-byte slots in one power-of-two array, kept at most half
// full. An erase shifts the later entries of its probe run back into the hole instead of leaving a
// tombstone, so lookups do not slow down as orders come and go -- the feed deletes nearly every order
// it adds.
class FlatOrderIndex {
 public:
  explicit FlatOrderIndex(size_t expected = 1024);

  uint32_t *find(uint64_t key) {
    for (size_t i = home(key);; i = (i + 1) & mask_) {
      Slot &s = slots_[i];
      if (!s.used)
        return nullptr;
      if (s.key == key)
        return &s.value;
    }
  }
  void insert(uint64_t key, uint32_t value);
  bool erase(uint64_t key);
  size_t size() const { return size_; }
  size_t capacity() const { return slots_.size(); }

 private:
  struct Slot {
    uint64_t key;
    uint32_t value;
    uint32_t used;
  };

  // Fibonacci hashing: multiply by 2^64 / golden ratio and keep the top bits. Order reference
  // numbers are mostly consecutive, and this spreads them over the whole table.
  size_t home(uint64_t key) const { return size_t((key * 0x9E3779B97F4A7C15ull) >> shift_); }
  void grow();

  std::vector<Slot> slots_;
  size_t mask_ = 0;
  size_t size_ = 0;
  int shift_ = 64;
};

// The same interface on std::unordered_map, for comparison.
class StdOrderIndex {
 public:
  explicit StdOrderIndex(size_t expected = 1024) { map_.reserve(expected); }

  uint32_t *find(uint64_t key) {
    auto it = map_.find(key);
    return it == map_.end() ? nullptr : &it->second;
  }
  void insert(uint64_t key, uint32_t value) { map_.emplace(key, value); }
  bool erase(uint64_t key) { return map_.erase(key) > 0; }
  size_t size() const { return map_.size(); }

 private:
  std::unordered_map<uint64_t, uint32_t> map_;
};

#endif
