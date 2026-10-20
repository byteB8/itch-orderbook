#ifndef HUGE_PAGES_H
#define HUGE_PAGES_H

// An allocator for large arrays that asks the kernel for 2 MB pages (transparent huge pages). The order
// pool and the order index are read at random addresses across more than 100 MB; with 4 KB pages most
// of those reads also miss the TLB, the CPU's cache of address translations, and wait for a page-table
// walk. A 2 MB page covers 512 times as much memory per translation. Memory comes straight from mmap and
// is marked with madvise(MADV_HUGEPAGE), which is what the kernel's default "madvise" setting for
// transparent huge pages needs; where no huge page is available, normal pages are used.

#include <sys/mman.h>

#include <cstddef>
#include <new>

namespace huge_pages {

// Whether new allocations ask for huge pages. On by default; benchmarks switch it off to compare.
inline bool &enabled() {
  static bool on = true;
  return on;
}

}  // namespace huge_pages

template <class T>
struct HugePageAllocator {
  using value_type = T;

  HugePageAllocator() = default;
  template <class U>
  HugePageAllocator(const HugePageAllocator<U> &) noexcept {}

  T *allocate(size_t n) {
    size_t bytes = roundUp(n * sizeof(T));
    void *p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
      throw std::bad_alloc();
    if (huge_pages::enabled())
      ::madvise(p, bytes, MADV_HUGEPAGE);
    return static_cast<T *>(p);
  }
  void deallocate(T *p, size_t n) noexcept { ::munmap(p, roundUp(n * sizeof(T))); }

  template <class U>
  bool operator==(const HugePageAllocator<U> &) const noexcept {
    return true;
  }

 private:
  static size_t roundUp(size_t bytes) {
    constexpr size_t page = size_t(2) << 20;
    return (bytes + page - 1) / page * page;
  }
};

#endif
