// Sends an ITCH file over UDP multicast as one MoldUDP64 session, at a set message rate.
//
//   publisher <itch file> [--group 239.192.0.1] [--port 30001] [--rate MESSAGES_PER_S] [--limit N]
//                         [--drop-every N] [--cpu N]
//
// --rate 0 sends as fast as it can. --drop-every N skips every Nth packet (its sequence numbers are used
// all the same), to exercise gap detection in the receiver. Packets carry up to 1,400 bytes. The session
// ends with an end-of-session packet, sent three times in case one is lost.
#include "cli.h"
#include "cpu.h"
#include "itch_file.h"
#include "moldudp64.h"
#include "udp.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>

int main(int argc, char **argv) {
  std::string file, group = "239.192.0.1";
  int port = 30001, cpu = -1;
  double rate = 1e6;
  uint64_t limit = 0, dropEvery = 0;
  for (int i = 1; i < argc; ++i) {
    const char *option = argv[i];
    auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (!std::strcmp(option, "--group"))
      group = next();
    else if (!std::strcmp(option, "--port"))
      port = optionValue<uint16_t>("publisher", option, next());
    else if (!std::strcmp(option, "--rate"))
      rate = optionValue<double>("publisher", option, next());
    else if (!std::strcmp(option, "--limit"))
      limit = optionValue<uint64_t>("publisher", option, next());
    else if (!std::strcmp(option, "--drop-every"))
      dropEvery = optionValue<uint64_t>("publisher", option, next());
    else if (!std::strcmp(option, "--cpu"))
      cpu = optionValue<int>("publisher", option, next());
    else
      file = option;
  }
  if (file.empty()) {
    std::fprintf(stderr, "usage: publisher <itch file> [--group G] [--port P] [--rate R] [--limit N] "
                         "[--drop-every N] [--cpu N]\n");
    return 2;
  }
  try {
    pinThisThread(cpu);
    ItchFile itch(file);
    MulticastSender tx(group, uint16_t(port));
    mold::Packer packer("ITCH50", 1400);
    uint64_t sequence = 1, sent = 0, packets = 0, dropped = 0;
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();

    auto flush = [&] {
      packer.finish();
      if (rate > 0) {  // the packet goes out when its first message is due
        auto due = start + std::chrono::nanoseconds(uint64_t(double(sequence - 1) * 1e9 / rate));
        while (Clock::now() < due) {
        }
      }
      if (dropEvery && (packets + 1) % dropEvery == 0)
        ++dropped;
      else
        tx.send(packer.data(), packer.size());
      ++packets;
      sequence += packer.count();
      packer.start(sequence);
    };

    packer.start(sequence);
    itch.forEach([&](const uint8_t *m, uint16_t len) {
      if (!packer.add(m, len)) {
        flush();
        packer.add(m, len);
      }
      return !limit || ++sent < limit;
    });
    if (limit == 0)
      sent = sequence - 1 + packer.count();
    if (packer.count())
      flush();
    double seconds = std::chrono::duration<double>(Clock::now() - start).count();

    packer.start(sequence);
    packer.finish(mold::kEndOfSession);
    for (int i = 0; i < 3; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      tx.send(packer.data(), packer.size());
    }
    std::printf("sent %llu messages in %llu packets (%llu dropped on purpose) in %.2f s: %.0f messages/s\n",
                (unsigned long long)sent, (unsigned long long)packets, (unsigned long long)dropped, seconds,
                sent / seconds);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "publisher: %s\n", e.what());
    return 1;
  }
}
