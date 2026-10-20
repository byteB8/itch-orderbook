#include "check.h"
#include "itch.h"
#include "itch_writer.h"

#include <cstdint>

int main() {
  // 09:30:00.000000123 is 34200000000123 ns after midnight
  const uint64_t ts = 34200000000123ull;

  auto add = ItchWriter().header('A', 13, ts).be(0x0102030405060708ull, 8).u8('B').be(100, 4).text("AAPL", 8)
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

  auto addF = ItchWriter().header('F', 13, ts).be(77, 8).u8('S').be(5, 4).text("AAPL", 8).be(2916000, 4)
                  .text("NITE", 4).bytes;
  CHECK_EQ(addF.size(), itch::messageLength('F'));
  auto f = itch::addOrder(addF.data());
  CHECK_EQ(f.ref, 77u);
  CHECK_EQ(f.side, 'S');
  CHECK_EQ(f.price, 2916000u);

  auto exec = ItchWriter().header('E', 13, ts).be(77, 8).be(3, 4).be(999, 8).bytes;
  CHECK_EQ(exec.size(), itch::messageLength('E'));
  CHECK_EQ(itch::orderExecuted(exec.data()).ref, 77u);
  CHECK_EQ(itch::orderExecuted(exec.data()).shares, 3u);

  auto execC = ItchWriter().header('C', 13, ts).be(77, 8).be(2, 4).be(1000, 8).u8('Y').be(2915000, 4).bytes;
  CHECK_EQ(execC.size(), itch::messageLength('C'));
  CHECK_EQ(itch::orderExecuted(execC.data()).shares, 2u);

  auto cancel = ItchWriter().header('X', 13, ts).be(77, 8).be(1, 4).bytes;
  CHECK_EQ(cancel.size(), itch::messageLength('X'));
  CHECK_EQ(itch::orderCancel(cancel.data()).shares, 1u);

  auto del = ItchWriter().header('D', 13, ts).be(0xFFFFFFFFFFFFFFFFull, 8).bytes;
  CHECK_EQ(del.size(), itch::messageLength('D'));
  CHECK_EQ(itch::orderDelete(del.data()), 0xFFFFFFFFFFFFFFFFull);

  auto replace = ItchWriter().header('U', 13, ts).be(77, 8).be(78, 8).be(40, 4).be(2914900, 4).bytes;
  CHECK_EQ(replace.size(), itch::messageLength('U'));
  auto u = itch::orderReplace(replace.data());
  CHECK_EQ(u.oldRef, 77u);
  CHECK_EQ(u.newRef, 78u);
  CHECK_EQ(u.shares, 40u);
  CHECK_EQ(u.price, 2914900u);

  auto dir = ItchWriter().header('R', 13, ts).text("AAPL", 8).text("Q", 1).text("N", 1).be(100, 4).text("N", 1)
                 .text("C", 1).text("Z", 2).text("P", 1).text("N", 1).text("N", 1).text("1", 1).text("N", 1)
                 .be(0, 4).text("N", 1).bytes;
  CHECK_EQ(dir.size(), itch::messageLength('R'));
  CHECK_EQ(itch::stockSymbol(dir.data()), "AAPL");

  CHECK_EQ(itch::messageLength('Z'), 0);
  CHECK_EQ(itch::be48(ItchWriter().be(0x0000ABCDEF012345ull, 6).bytes.data()), 0xABCDEF012345ull);

  return report("test_itch");
}
