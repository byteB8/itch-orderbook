#ifndef LATENCY_H
#define LATENCY_H

// Timing for latency measurements: a clock based on the CPU's time-stamp counter, and a histogram.

#include <x86intrin.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

// Reading the time-stamp counter takes about 20 cycles, against about 20 ns for clock_gettime. On CPUs
// with an invariant TSC (constant_tsc and nonstop_tsc in /proc/cpuinfo) it ticks at a constant rate on
// every core; that rate is measured once against steady_clock.
class TscClock {
 public:
  TscClock() {
    auto t0 = std::chrono::steady_clock::now();
    uint64_t c0 = __rdtsc();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(50)) {
    }
    auto t1 = std::chrono::steady_clock::now();
    uint64_t c1 = __rdtsc();
    nsPerTick_ = std::chrono::duration<double, std::nano>(t1 - t0).count() / double(c1 - c0);
  }
  static uint64_t now() { return __rdtsc(); }
  uint64_t toNs(uint64_t ticks) const { return uint64_t(double(ticks) * nsPerTick_); }

 private:
  double nsPerTick_ = 1.0;
};

// Counts in 1 ns buckets up to 1 ms, and one bucket for anything longer; percentiles are exact to the
// nanosecond below 1 ms.
class LatencyHistogram {
 public:
  void add(uint64_t ns) {
    ++counts_[std::min(ns, kLimit)];
    ++n_;
    max_ = std::max(max_, ns);
  }
  // The smallest value at or below which p percent of the samples fall.
  uint64_t percentile(double p) const {
    if (n_ == 0)
      return 0;
    uint64_t target = std::max<uint64_t>(1, uint64_t(p / 100.0 * double(n_) + 0.5));
    uint64_t seen = 0;
    for (uint64_t ns = 0; ns <= kLimit; ++ns) {
      seen += counts_[ns];
      if (seen >= target)
        return ns == kLimit ? max_ : ns;
    }
    return max_;
  }
  uint64_t count() const { return n_; }
  uint64_t max() const { return max_; }
  const std::vector<uint64_t> &buckets() const { return counts_; }

 private:
  static constexpr uint64_t kLimit = 1'000'000;
  std::vector<uint64_t> counts_ = std::vector<uint64_t>(kLimit + 1);
  uint64_t n_ = 0;
  uint64_t max_ = 0;
};

#endif
