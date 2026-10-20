#ifndef ITCH_WRITER_H
#define ITCH_WRITER_H

// Builds ITCH 5.0 messages for tests, field by field and big-endian, as NASDAQ sends them.

#include <cstdint>
#include <string>
#include <vector>

struct ItchWriter {
  std::vector<uint8_t> bytes;
  ItchWriter &u8(uint8_t v) {
    bytes.push_back(v);
    return *this;
  }
  ItchWriter &be(uint64_t v, int n) {
    for (int i = n - 1; i >= 0; --i)
      bytes.push_back(uint8_t(v >> (8 * i)));
    return *this;
  }
  ItchWriter &text(const std::string &s, size_t n) {
    for (size_t i = 0; i < n; ++i)
      bytes.push_back(i < s.size() ? uint8_t(s[i]) : uint8_t(' '));
    return *this;
  }
  ItchWriter &header(char type, uint16_t locate, uint64_t timestamp = 0) {
    return u8(uint8_t(type)).be(locate, 2).be(0, 2).be(timestamp, 6);
  }
};

inline std::vector<uint8_t> addMsg(uint16_t locate, uint64_t ref, char side, uint32_t shares, uint32_t price) {
  return ItchWriter().header('A', locate).be(ref, 8).u8(uint8_t(side)).be(shares, 4).text("TEST", 8).be(price, 4).bytes;
}
inline std::vector<uint8_t> executeMsg(uint16_t locate, uint64_t ref, uint32_t shares) {
  return ItchWriter().header('E', locate).be(ref, 8).be(shares, 4).be(0, 8).bytes;
}
inline std::vector<uint8_t> cancelMsg(uint16_t locate, uint64_t ref, uint32_t shares) {
  return ItchWriter().header('X', locate).be(ref, 8).be(shares, 4).bytes;
}
inline std::vector<uint8_t> deleteMsg(uint16_t locate, uint64_t ref) {
  return ItchWriter().header('D', locate).be(ref, 8).bytes;
}
inline std::vector<uint8_t> replaceMsg(uint16_t locate, uint64_t oldRef, uint64_t newRef, uint32_t shares,
                                       uint32_t price) {
  return ItchWriter().header('U', locate).be(oldRef, 8).be(newRef, 8).be(shares, 4).be(price, 4).bytes;
}

#endif
