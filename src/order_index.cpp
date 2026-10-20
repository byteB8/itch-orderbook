#include "order_index.h"

FlatOrderIndex::FlatOrderIndex(size_t expected) {
  size_t capacity = 16;
  while (capacity < 2 * expected)
    capacity *= 2;
  slots_.assign(capacity, Slot{0, 0, 0});
  mask_ = capacity - 1;
  shift_ = 64 - __builtin_ctzll(capacity);
}

void FlatOrderIndex::insert(uint64_t key, uint32_t value) {
  if (2 * (size_ + 1) > slots_.size())
    grow();
  size_t i = home(key);
  while (slots_[i].used)
    i = (i + 1) & mask_;
  slots_[i] = Slot{key, value, 1};
  ++size_;
}

bool FlatOrderIndex::erase(uint64_t key) {
  size_t hole = home(key);
  for (;; hole = (hole + 1) & mask_) {
    if (!slots_[hole].used)
      return false;
    if (slots_[hole].key == key)
      break;
  }
  // Backward-shift deletion. An entry later in the run may move into the hole unless its home slot
  // lies (cyclically) in (hole, j]: then the hole is before its home, and a lookup would not reach it.
  for (size_t j = (hole + 1) & mask_; slots_[j].used; j = (j + 1) & mask_) {
    size_t h = home(slots_[j].key);
    bool stays = hole < j ? (hole < h && h <= j) : (hole < h || h <= j);
    if (!stays) {
      slots_[hole] = slots_[j];
      hole = j;
    }
  }
  slots_[hole].used = 0;
  --size_;
  return true;
}

void FlatOrderIndex::grow() {
  std::vector<Slot, HugePageAllocator<Slot>> old(slots_.size() * 2, Slot{0, 0, 0});
  old.swap(slots_);
  mask_ = slots_.size() - 1;
  --shift_;
  size_ = 0;
  for (const Slot &s : old)
    if (s.used)
      insert(s.key, s.value);
}
