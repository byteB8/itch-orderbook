#ifndef ITCH_H
#define ITCH_H

// NASDAQ TotalView-ITCH 5.0 messages: the ones that change the visible order book, plus the stock
// directory. A message starts with its type byte, then a common header: stock locate (2 bytes),
// tracking number (2) and a timestamp (6, nanoseconds since midnight). All integers are big-endian.
// Prices are unsigned 4-byte integers with four implied decimal places: 2915200 is $291.52.

#include <cstdint>
#include <cstring>
#include <string_view>

namespace itch {

inline uint16_t be16(const uint8_t *p) {
  uint16_t v;
  std::memcpy(&v, p, 2);
  return __builtin_bswap16(v);
}
inline uint32_t be32(const uint8_t *p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return __builtin_bswap32(v);
}
inline uint64_t be48(const uint8_t *p) { return uint64_t(be16(p)) << 32 | be32(p + 2); }
inline uint64_t be64(const uint8_t *p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return __builtin_bswap64(v);
}

// Message lengths in bytes, type byte included; 0 for types this project does not decode.
constexpr uint16_t messageLength(char type) {
  switch (type) {
    case 'S': return 12;  // system event
    case 'R': return 39;  // stock directory
    case 'A': return 36;  // add order
    case 'F': return 40;  // add order with market participant attribution
    case 'E': return 31;  // order executed
    case 'C': return 36;  // order executed with price
    case 'X': return 23;  // order cancel (part of the shares)
    case 'D': return 19;  // order delete
    case 'U': return 35;  // order replace
    case 'P': return 44;  // trade of a non-displayed order: does not change the visible book
    default: return 0;
  }
}

struct Header {
  char type;
  uint16_t locate;     // the instrument, 1..N for the day
  uint64_t timestamp;  // nanoseconds since midnight
};

inline Header header(const uint8_t *m) { return {char(m[0]), be16(m + 1), be48(m + 5)}; }

// 'A' and 'F'. 'F' adds a 4-byte attribution after the price, which the book does not need.
struct AddOrder {
  uint64_t ref;  // order reference number, unique for the day
  char side;     // 'B' buy, 'S' sell
  uint32_t shares;
  uint32_t price;
};
inline AddOrder addOrder(const uint8_t *m) { return {be64(m + 11), char(m[19]), be32(m + 20), be32(m + 32)}; }

// 'E' and 'C': shares of a resting order traded. 'C' carries its own price, which can differ from
// the order's (for example in a cross); the book only needs the shares.
struct OrderExecuted {
  uint64_t ref;
  uint32_t shares;
};
inline OrderExecuted orderExecuted(const uint8_t *m) { return {be64(m + 11), be32(m + 19)}; }

// 'X': shares removed from an order that stays on the book with the rest.
struct OrderCancel {
  uint64_t ref;
  uint32_t shares;
};
inline OrderCancel orderCancel(const uint8_t *m) { return {be64(m + 11), be32(m + 19)}; }

// 'D': the whole order leaves the book.
inline uint64_t orderDelete(const uint8_t *m) { return be64(m + 11); }

// 'U': the original order leaves the book; a new one, on the same side, takes its place with a new
// reference number, size and price, and no time priority.
struct OrderReplace {
  uint64_t oldRef;
  uint64_t newRef;
  uint32_t shares;
  uint32_t price;
};
inline OrderReplace orderReplace(const uint8_t *m) {
  return {be64(m + 11), be64(m + 19), be32(m + 27), be32(m + 31)};
}

// 'R': the ticker of a stock locate, space-padded to 8 characters.
inline std::string_view stockSymbol(const uint8_t *m) {
  std::string_view s(reinterpret_cast<const char *>(m + 11), 8);
  return s.substr(0, s.find_last_not_of(' ') + 1);
}

}  // namespace itch

#endif
