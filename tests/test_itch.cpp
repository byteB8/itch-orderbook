#include "check.h"
#include "itch.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

// Builds a message field by field, big-endian, the way NASDAQ sends it.
struct Writer {
  std::vector<uint8_t> bytes;
  Writer &u8(uint8_t v) { bytes.push_back(v); return *this; }
  Writer &be(uint64_t v, int n) {
    for (int i = n - 1; i >= 0; --i)
      bytes.push_back(uint8_t(v >> (8 * i)));
    return *this;
  }
  Writer &text(const std::string &s, size_t n) {
    for (size_t i = 0; i < n; ++i)
      bytes.push_back(i < s.size() ? uint8_t(s[i]) : uint8_t(' '));
    return *this;
  }
  Writer &header(char type, uint16_t locate, uint64_t timestamp) {
    return u8(uint8_t(type)).be(locate, 2).be(0, 2).be(timestamp, 6);
  }
};

}  // namespace

int main() {
  // 09:30:00.000000123 is 34200000000123 ns after midnight
  const uint64_t ts = 34200000000123ull;

  auto add = Writer().header('A', 13, ts).be(0x0102030405060708ull, 8).u8('B').be(100, 4).text("AAPL", 8)
                 .be(2915200, 4).bytes;
  CHECK_EQ(add.size(), itch::messageLength('A'));
  auto h = itch::header(add.data());
  CHECK_EQ(h.type, 'A');
  CHECK_EQ(h.locate, 13);
  CHECK_EQ(h.timestamp, ts);
  auto a = itch::addOrder(add.data());
  CHECK_EQ(a.ref, 0x0102030405060708ull);
  CHECK_EQ(a.side, 'B');
  CHECK_EQ(a.shares, 100u);
  CHECK_EQ(a.price, 2915200u);

  auto addF = Writer().header('F', 13, ts).be(77, 8).u8('S').be(5, 4).text("AAPL", 8).be(2916000, 4)
                  .text("NITE", 4).bytes;
  CHECK_EQ(addF.size(), itch::messageLength('F'));
  auto f = itch::addOrder(addF.data());
  CHECK_EQ(f.ref, 77u);
  CHECK_EQ(f.side, 'S');
  CHECK_EQ(f.price, 2916000u);

  auto exec = Writer().header('E', 13, ts).be(77, 8).be(3, 4).be(999, 8).bytes;
  CHECK_EQ(exec.size(), itch::messageLength('E'));
  CHECK_EQ(itch::orderExecuted(exec.data()).ref, 77u);
  CHECK_EQ(itch::orderExecuted(exec.data()).shares, 3u);

  auto execC = Writer().header('C', 13, ts).be(77, 8).be(2, 4).be(1000, 8).u8('Y').be(2915000, 4).bytes;
  CHECK_EQ(execC.size(), itch::messageLength('C'));
  CHECK_EQ(itch::orderExecuted(execC.data()).shares, 2u);

  auto cancel = Writer().header('X', 13, ts).be(77, 8).be(1, 4).bytes;
  CHECK_EQ(cancel.size(), itch::messageLength('X'));
  CHECK_EQ(itch::orderCancel(cancel.data()).shares, 1u);

  auto del = Writer().header('D', 13, ts).be(0xFFFFFFFFFFFFFFFFull, 8).bytes;
  CHECK_EQ(del.size(), itch::messageLength('D'));
  CHECK_EQ(itch::orderDelete(del.data()), 0xFFFFFFFFFFFFFFFFull);

  auto replace = Writer().header('U', 13, ts).be(77, 8).be(78, 8).be(40, 4).be(2914900, 4).bytes;
  CHECK_EQ(replace.size(), itch::messageLength('U'));
  auto u = itch::orderReplace(replace.data());
  CHECK_EQ(u.oldRef, 77u);
  CHECK_EQ(u.newRef, 78u);
  CHECK_EQ(u.shares, 40u);
  CHECK_EQ(u.price, 2914900u);

  auto dir = Writer().header('R', 13, ts).text("AAPL", 8).text("Q", 1).text("N", 1).be(100, 4).text("N", 1)
                 .text("C", 1).text("Z", 2).text("P", 1).text("N", 1).text("N", 1).text("1", 1).text("N", 1)
                 .be(0, 4).text("N", 1).bytes;
  CHECK_EQ(dir.size(), itch::messageLength('R'));
  CHECK_EQ(itch::stockSymbol(dir.data()), "AAPL");

  CHECK_EQ(itch::messageLength('Z'), 0);
  CHECK_EQ(itch::be48(Writer().be(0x0000ABCDEF012345ull, 6).bytes.data()), 0xABCDEF012345ull);

  return report("test_itch");
}
