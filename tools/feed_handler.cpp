// Receives an ITCH feed over UDP multicast (MoldUDP64) and keeps order books for every instrument. It
// measures the latency from receiving a packet to having applied each of its messages to the books.
//
//   feed_handler [--group 239.192.0.1] [--port 30001] [--threads 1|2] [--rx-cpu N] [--book-cpu M]
//                [--label L] [--result results.csv] [--hist histogram.csv]
//
// --threads 2: the receive thread polls the socket and hands each message to a book thread through an
//              SPSC queue. --threads 1: the receive thread applies the messages itself.
// It stops at the end-of-session packet, or 5 s after the last packet.
#include "cpu.h"
#include "feed_book.h"
#include "itch.h"
#include "latency.h"
#include "moldudp64.h"
#include "spsc_queue.h"
#include "udp.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

// One message on its way from the receive thread to the book thread: 64 bytes, one cache line.
// ITCH 5.0 messages are at most 50 bytes long.
struct Slot {
  uint64_t received;  // TSC ticks when its packet arrived
  uint16_t length;
  uint8_t bytes[50];
};
static_assert(sizeof(Slot) == 64);

struct Options {
  std::string group = "239.192.0.1", label = "run", result, hist;
  int port = 30001, threads = 2, rxCpu = -1, bookCpu = -1;
};

// Written by the receive thread only; on its own cache line, so the book thread's writes to its
// counters do not bounce this line between the two cores (false sharing).
struct alignas(64) Counters {
  uint64_t packets = 0, malformed = 0, tooLong = 0;
};

// Written by whichever thread applies messages to the books.
struct alignas(64) Applied {
  uint64_t messages = 0, firstTicks = 0, lastTicks = 0;
};

// Polls the socket until the end-of-session packet, or 5 s of silence after the first packet. Calls
// onMessage(message, length, receivedTicks) for every message not seen before, in sequence order.
template <class F>
void receive(MulticastReceiver &rx, mold::SequenceTracker &tracker, Counters &c, F onMessage) {
  std::vector<uint8_t> buf(65536);
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  uint64_t idle = 0;
  for (;;) {
    ssize_t n = rx.receive(buf.data(), buf.size());
    if (n <= 0) {
      if (n < 0 || (c.packets > 0 && ++idle % 1'000'000 == 0 && Clock::now() - last > std::chrono::seconds(5)))
        return;
      continue;
    }
    uint64_t received = TscClock::now();
    idle = 0;
    last = Clock::now();
    if (size_t(n) < mold::kHeaderBytes) {
      ++c.malformed;
      continue;
    }
    uint16_t count = itch::be16(buf.data() + 18);
    if (count == mold::kEndOfSession)
      return;
    ++c.packets;
    mold::SequenceTracker::Verdict v = tracker.onPacket(itch::be64(buf.data() + 10), count);
    uint64_t index = 0;
    mold::Header h{};
    bool ok = mold::parse(buf.data(), size_t(n), h, [&](const uint8_t *m, uint16_t len) {
      if (index++ >= v.skip)
        onMessage(m, len, received);
    });
    c.malformed += !ok;
  }
}

}  // namespace

int main(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string("0"); };
    if (!std::strcmp(argv[i], "--group"))
      opt.group = next();
    else if (!std::strcmp(argv[i], "--port"))
      opt.port = std::stoi(next());
    else if (!std::strcmp(argv[i], "--threads"))
      opt.threads = std::stoi(next());
    else if (!std::strcmp(argv[i], "--rx-cpu"))
      opt.rxCpu = std::stoi(next());
    else if (!std::strcmp(argv[i], "--book-cpu"))
      opt.bookCpu = std::stoi(next());
    else if (!std::strcmp(argv[i], "--label"))
      opt.label = next();
    else if (!std::strcmp(argv[i], "--result"))
      opt.result = next();
    else if (!std::strcmp(argv[i], "--hist"))
      opt.hist = next();
    else {
      std::fprintf(stderr, "feed_handler: unknown option %s\n", argv[i]);
      return 2;
    }
  }
  try {
    TscClock clock;
    MulticastReceiver rx(opt.group, uint16_t(opt.port));
    auto feed = std::make_unique<DefaultFeedBook>();
    LatencyHistogram latency;
    mold::SequenceTracker tracker;
    Counters c;
    Applied a;

    auto apply = [&](const uint8_t *m, uint64_t received) {
      feed->apply(m);
      uint64_t now = TscClock::now();
      latency.add(clock.toNs(now - received));
      if (!a.firstTicks)
        a.firstTicks = received;
      a.lastTicks = now;
      ++a.messages;
    };

    if (opt.threads == 1) {
      pinThisThread(opt.rxCpu);
      receive(rx, tracker, c, [&](const uint8_t *m, uint16_t, uint64_t received) { apply(m, received); });
    } else {
      auto queue = std::make_unique<SpscQueue<Slot, 1 << 16>>();
      std::atomic<bool> receiving{true};
      std::thread book([&] {
        pinThisThread(opt.bookCpu);
        Slot s;
        for (;;) {
          if (queue->tryPop(s)) {
            apply(s.bytes, s.received);
          } else if (!receiving.load(std::memory_order_acquire)) {
            while (queue->tryPop(s))  // everything pushed before receiving went false is visible now
              apply(s.bytes, s.received);
            return;
          }
        }
      });
      pinThisThread(opt.rxCpu);
      receive(rx, tracker, c, [&](const uint8_t *m, uint16_t len, uint64_t received) {
        if (len > sizeof(Slot::bytes)) {
          ++c.tooLong;
          return;
        }
        Slot s;
        s.received = received;
        s.length = len;
        std::memcpy(s.bytes, m, len);
        while (!queue->tryPush(s)) {
        }
      });
      receiving.store(false, std::memory_order_release);
      book.join();
    }

    double seconds = clock.toNs(a.lastTicks - a.firstTicks) / 1e9;
    const FeedStats &s = feed->stats();
    std::printf("%s: %llu packets, %llu messages in %.2f s (%.0f messages/s); missed %llu, duplicates %llu, "
                "malformed %llu; receive buffer %d bytes\n",
                opt.label.c_str(), (unsigned long long)c.packets, (unsigned long long)a.messages, seconds,
                a.messages / seconds, (unsigned long long)tracker.gaps(), (unsigned long long)tracker.duplicates(),
                (unsigned long long)c.malformed, rx.bufferBytes());
    std::printf("books: unknown orders %llu, over-executed %llu, orders on the book %zu\n",
                (unsigned long long)s.unknownOrders, (unsigned long long)s.overExecuted, feed->liveOrders());
    std::printf("latency, packet received to message applied (ns): p50 %llu  p90 %llu  p99 %llu  p99.9 %llu  "
                "max %llu\n",
                (unsigned long long)latency.percentile(50), (unsigned long long)latency.percentile(90),
                (unsigned long long)latency.percentile(99), (unsigned long long)latency.percentile(99.9),
                (unsigned long long)latency.max());

    if (!opt.result.empty()) {
      FILE *out = std::fopen(opt.result.c_str(), "a");
      if (!out)
        throw std::runtime_error("cannot write " + opt.result);
      if (std::ftell(out) == 0)
        std::fprintf(out, "label,threads,packets,messages,seconds,missed,unknown_orders,p50_ns,p90_ns,p99_ns,"
                          "p999_ns,max_ns\n");
      std::fprintf(out, "%s,%d,%llu,%llu,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", opt.label.c_str(), opt.threads,
                   (unsigned long long)c.packets, (unsigned long long)a.messages, seconds,
                   (unsigned long long)tracker.gaps(), (unsigned long long)s.unknownOrders,
                   (unsigned long long)latency.percentile(50), (unsigned long long)latency.percentile(90),
                   (unsigned long long)latency.percentile(99), (unsigned long long)latency.percentile(99.9),
                   (unsigned long long)latency.max());
      std::fclose(out);
    }
    if (!opt.hist.empty()) {
      FILE *out = std::fopen(opt.hist.c_str(), "w");
      if (!out)
        throw std::runtime_error("cannot write " + opt.hist);
      std::fprintf(out, "ns,count\n");
      latency.forEachBucket([&](uint64_t ns, uint64_t n) {
        std::fprintf(out, "%llu,%llu\n", (unsigned long long)ns, (unsigned long long)n);
      });
      std::fclose(out);
    }
  } catch (const std::exception &e) {
    std::fprintf(stderr, "feed_handler: %s\n", e.what());
    return 1;
  }
}
