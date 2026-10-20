#include "check.h"
#include "latency.h"
#include "udp.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

void histogram() {
  LatencyHistogram h;
  for (uint64_t ns = 1; ns <= 100; ++ns)
    h.add(ns);
  CHECK_EQ(h.percentile(50), 50u);
  CHECK_EQ(h.percentile(99), 99u);
  CHECK_EQ(h.percentile(100), 100u);
  h.add(5'000'000);  // beyond the 1 ms buckets: reported as the maximum
  CHECK_EQ(h.percentile(100), 5'000'000u);
  CHECK_EQ(h.count(), 101u);
}

void tscClock() {
  TscClock clock;
  uint64_t c0 = TscClock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  uint64_t ns = clock.toNs(TscClock::now() - c0);
  CHECK(ns > 19'000'000 && ns < 30'000'000);
}

// Datagrams sent to a multicast group on loopback arrive intact and in order.
void loopbackMulticast() {
  MulticastReceiver rx("239.192.0.7", 30107);
  MulticastSender tx("239.192.0.7", 30107);
  size_t got = 0, wrong = 0;
  std::vector<uint8_t> buf(2048);
  for (uint8_t i = 0; i < 100; ++i) {
    std::vector<uint8_t> packet(100 + i, i);
    CHECK(tx.send(packet.data(), packet.size()));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    ssize_t n = 0;
    while ((n = rx.receive(buf.data(), buf.size())) == 0 && std::chrono::steady_clock::now() < deadline) {
    }
    got += n > 0;
    wrong += n != ssize_t(packet.size()) || std::vector<uint8_t>(buf.begin(), buf.begin() + n) != packet;
  }
  CHECK_EQ(got, 100u);
  CHECK_EQ(wrong, 0u);
  CHECK(rx.bufferBytes() > 0);
}

}  // namespace

int main() {
  histogram();
  tscClock();
  loopbackMulticast();
  return report("test_net");
}
