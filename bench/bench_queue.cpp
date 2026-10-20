// Hand-off between two threads: the lock-free SPSC queue against the same ring protected by a std::mutex.
// Both sides busy-poll, so the difference is the cost of the lock and of the cache lines it moves.
//
//   bench_queue <out_dir> [--pair CPU_A,CPU_B,label]...
//
// For each pair of CPUs:
// - throughput: one thread pushes 20M 64-byte items, the other pops them;
// - round trip: one thread sends an item and waits for it to come back through a second queue, 1M times;
//   half the round trip is the one-way hand-off latency.
// Writes queue.csv.
#include "cpu.h"
#include "latency.h"
#include "spsc_queue.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct alignas(64) Item {
  uint64_t seq;
  uint8_t payload[56];
};

template <class T, size_t Capacity>
class MutexQueue {
 public:
  bool tryPush(const T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tail_ - head_ == Capacity)
      return false;
    slots_[tail_++ & (Capacity - 1)] = value;
    return true;
  }
  bool tryPop(T &out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (head_ == tail_)
      return false;
    out = slots_[head_++ & (Capacity - 1)];
    return true;
  }

 private:
  std::mutex mutex_;
  size_t head_ = 0, tail_ = 0;
  std::array<T, Capacity> slots_;
};

constexpr size_t kCapacity = 1024;
using Spsc = SpscQueue<Item, kCapacity>;
using Locked = MutexQueue<Item, kCapacity>;

template <class Queue>
double throughput(int cpuA, int cpuB, uint64_t n) {
  auto q = std::make_unique<Queue>();
  std::atomic<bool> go{false};
  std::thread consumer([&] {
    pinThisThread(cpuB);
    Item it;
    while (!go.load(std::memory_order_acquire)) {
    }
    for (uint64_t got = 0; got < n;)
      got += q->tryPop(it);
  });
  pinThisThread(cpuA);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let the consumer reach its CPU
  auto start = std::chrono::steady_clock::now();
  go.store(true, std::memory_order_release);
  Item it{};
  for (uint64_t i = 0; i < n; ++i) {
    it.seq = i;
    while (!q->tryPush(it)) {
    }
  }
  consumer.join();
  return double(n) / std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

template <class Queue>
LatencyHistogram roundTrip(int cpuA, int cpuB, uint64_t n, const TscClock &clock) {
  auto there = std::make_unique<Queue>(), back = std::make_unique<Queue>();
  std::thread echo([&] {
    pinThisThread(cpuB);
    Item it;
    for (uint64_t i = 0; i < n; ++i) {
      while (!there->tryPop(it)) {
      }
      while (!back->tryPush(it)) {
      }
    }
  });
  pinThisThread(cpuA);
  LatencyHistogram h;
  Item it{};
  for (uint64_t i = 0; i < n; ++i) {
    uint64_t t0 = TscClock::now();
    it.seq = i;
    while (!there->tryPush(it)) {
    }
    while (!back->tryPop(it)) {
    }
    h.add(clock.toNs(TscClock::now() - t0));
  }
  echo.join();
  return h;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: bench_queue <out_dir> [--pair CPU_A,CPU_B,label]...\n");
    return 2;
  }
  struct Pair {
    int a, b;
    std::string label;
  };
  std::vector<Pair> pairs;
  for (int i = 2; i + 1 < argc; i += 2)
    if (!std::strcmp(argv[i], "--pair")) {
      Pair p{};
      char label[64] = {0};
      if (std::sscanf(argv[i + 1], "%d,%d,%63s", &p.a, &p.b, label) == 3) {
        p.label = label;
        pairs.push_back(p);
      }
    }
  if (pairs.empty())
    pairs.push_back({0, 1, "default"});

  TscClock clock;
  FILE *csv = std::fopen((std::string(argv[1]) + "/queue.csv").c_str(), "a");
  if (!csv) {
    std::fprintf(stderr, "bench_queue: cannot write to %s\n", argv[1]);
    return 1;
  }
  if (std::ftell(csv) == 0)
    std::fprintf(csv, "queue,cpu_a,cpu_b,pair,messages_per_s,rtt_p50_ns,rtt_p99_ns,rtt_p999_ns\n");
  for (const Pair &p : pairs) {
    auto run = [&](const char *name, auto throughputFn, auto roundTripFn) {
      double mps = throughputFn();
      LatencyHistogram h = roundTripFn();
      std::fprintf(csv, "%s,%d,%d,%s,%.0f,%llu,%llu,%llu\n", name, p.a, p.b, p.label.c_str(), mps,
                   (unsigned long long)h.percentile(50), (unsigned long long)h.percentile(99),
                   (unsigned long long)h.percentile(99.9));
      std::printf("%-6s CPUs %d,%d (%s): %.1fM items/s; round trip p50 %llu ns, p99 %llu ns, p99.9 %llu ns\n", name,
                  p.a, p.b, p.label.c_str(), mps / 1e6, (unsigned long long)h.percentile(50),
                  (unsigned long long)h.percentile(99), (unsigned long long)h.percentile(99.9));
      std::fflush(stdout);
    };
    run("spsc", [&] { return throughput<Spsc>(p.a, p.b, 20'000'000); },
        [&] { return roundTrip<Spsc>(p.a, p.b, 1'000'000, clock); });
    run("mutex", [&] { return throughput<Locked>(p.a, p.b, 20'000'000); },
        [&] { return roundTrip<Locked>(p.a, p.b, 1'000'000, clock); });
  }
  std::fclose(csv);
}
