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

// Counts in 1 ns buckets up to 1 ms, then 1 us buckets up to 1 s, and one bucket for anything longer.
// Percentiles are exact to the nanosecond below 1 ms and to the microsecond below 1 s.
class LatencyHistogram {
 public:
  void add(uint64_t ns) {
    if (ns < kFine)
      ++fine_[ns];
    else if (ns < kCoarse)
      ++coarse_[ns / 1000];
    else
      ++over_;
    ++n_;
    max_ = std::max(max_, ns);
  }
  // The smallest value at or below which p percent of the samples fall.
  uint64_t percentile(double p) const {
    if (n_ == 0)
      return 0;
    uint64_t target = std::max<uint64_t>(1, uint64_t(p / 100.0 * double(n_) + 0.5));
    uint64_t seen = 0;
    for (uint64_t ns = 0; ns < kFine; ++ns)
      if ((seen += fine_[ns]) >= target)
        return ns;
    for (uint64_t us = kFine / 1000; us < kCoarse / 1000; ++us)
      if ((seen += coarse_[us]) >= target)
        return us * 1000;
    return max_;
  }
  uint64_t count() const { return n_; }
  uint64_t max() const { return max_; }
  // Calls f(lowest ns of the bucket, count) for every non-empty bucket, in order.
  template <class F>
  void forEachBucket(F f) const {
    for (uint64_t ns = 0; ns < kFine; ++ns)
      if (fine_[ns])
        f(ns, fine_[ns]);
    for (uint64_t us = kFine / 1000; us < kCoarse / 1000; ++us)
      if (coarse_[us])
        f(us * 1000, coarse_[us]);
    if (over_)
      f(kCoarse, over_);
  }

 private:
  static constexpr uint64_t kFine = 1'000'000;        // 1 ms
  static constexpr uint64_t kCoarse = 1'000'000'000;  // 1 s
  std::vector<uint64_t> fine_ = std::vector<uint64_t>(kFine);
  std::vector<uint64_t> coarse_ = std::vector<uint64_t>(kCoarse / 1000);
  uint64_t over_ = 0;
  uint64_t n_ = 0;
  uint64_t max_ = 0;
};

#endif
