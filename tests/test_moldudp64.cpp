#include "check.h"
#include "itch_writer.h"
#include "moldudp64.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

void packAndParse() {
  mold::Packer packer("TEST", 100);
  packer.start(41);
  auto a = addMsg(1, 10, 'B', 100, 2915200);  // 36 bytes
  auto d = deleteMsg(1, 10);                   // 19 bytes
  CHECK(packer.add(a.data(), uint16_t(a.size())));
  CHECK(packer.add(d.data(), uint16_t(d.size())));
  CHECK(!packer.add(a.data(), uint16_t(a.size())));  // 20 + 38 + 21 + 38 > 100
  packer.finish();
  CHECK_EQ(packer.size(), 20u + 38 + 21);

  mold::Header h{};
  std::vector<std::vector<uint8_t>> got;
  bool ok = mold::parse(packer.data(), packer.size(), h,
                        [&](const uint8_t *m, uint16_t len) { got.emplace_back(m, m + len); });
  CHECK(ok);
  CHECK_EQ(std::string(h.session, 10), "TEST      ");
  CHECK_EQ(h.sequence, 41u);
  CHECK_EQ(h.count, 2);
  CHECK(got.size() == 2 && got[0] == a && got[1] == d);

  // a truncated packet is rejected
  CHECK(!mold::parse(packer.data(), packer.size() - 1, h, [](const uint8_t *, uint16_t) {}));

  // end of session: a header with count 0xFFFF and nothing after it
  packer.start(43);
  packer.finish(mold::kEndOfSession);
  CHECK(mold::parse(packer.data(), packer.size(), h, [](const uint8_t *, uint16_t) {}));
  CHECK_EQ(h.count, mold::kEndOfSession);
}

void sequenceTracking() {
  mold::SequenceTracker t;
  auto v = t.onPacket(1, 5);  // messages 1-5
  CHECK(v.skip == 0 && v.gap == 0);
  v = t.onPacket(9, 3);       // 6-8 lost
  CHECK(v.skip == 0 && v.gap == 3);
  CHECK_EQ(t.next(), 12u);
  v = t.onPacket(9, 3);       // the same packet again
  CHECK(v.skip == 3 && v.gap == 0);
  v = t.onPacket(10, 4);      // 10-11 seen, 12-13 new
  CHECK(v.skip == 2 && v.gap == 0);
  CHECK_EQ(t.next(), 14u);
  v = t.onPacket(14, 0);      // a heartbeat carries the next sequence number
  CHECK(v.skip == 0 && v.gap == 0);
  CHECK_EQ(t.gaps(), 3u);
  CHECK_EQ(t.duplicates(), 5u);
}

// Losses that no later data packet reveals: just before the end of the session, and inside a packet that
// was cut short.
void lossesAtTheEdges() {
  mold::SequenceTracker t;
  t.onPacket(1, 5);       // messages 1-5
  t.onEndOfSession(9);    // the session ended after message 8: 6-8 were lost
  CHECK_EQ(t.gaps(), 3u);

  mold::SequenceTracker c;
  auto v = c.onPacket(1, 4);  // messages 1-4, but only message 1 arrived intact
  c.cutShort(v, 1, 4);
  CHECK_EQ(c.gaps(), 3u);
  CHECK_EQ(c.next(), 5u);
  v = c.onPacket(3, 4);       // 3-4 already counted, 5-6 new; again only the first message arrived
  c.cutShort(v, 1, 4);
  CHECK_EQ(c.gaps(), 5u);
  v = c.onPacket(7, 2);       // in sequence: the cut messages are not counted twice
  CHECK(v.skip == 0 && v.gap == 0);
  CHECK_EQ(c.gaps(), 5u);
}

}  // namespace

int main() {
  packAndParse();
  sequenceTracking();
  lossesAtTheEdges();
  return report("test_moldudp64");
}
