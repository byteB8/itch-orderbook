#ifndef MOLDUDP64_H
#define MOLDUDP64_H

// MoldUDP64, the framing NASDAQ uses to send ITCH over UDP multicast. A packet has a 20-byte header --
// session (10 ASCII bytes), the sequence number of its first message (8 bytes) and a message count
// (2 bytes), big-endian -- and then the messages, each preceded by its 2-byte length. A count of 0 is a
// heartbeat; 0xFFFF marks the end of the session.

#include "itch.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace mold {

constexpr size_t kHeaderBytes = 20;
constexpr uint16_t kEndOfSession = 0xFFFF;

struct Header {
  char session[10];
  uint64_t sequence;  // of the first message in the packet
  uint16_t count;
};

// Fills one packet at a time, up to maxBytes (1,400 keeps a packet within a 1,500-byte Ethernet frame).
class Packer {
 public:
  explicit Packer(std::string_view session, size_t maxBytes = 1400) : maxBytes_(maxBytes) {
    std::memset(session_, ' ', sizeof session_);
    std::memcpy(session_, session.data(), std::min(session.size(), sizeof session_));
  }

  // Starts a packet whose first message will have this sequence number.
  void start(uint64_t sequence) {
    buf_.assign(kHeaderBytes, 0);
    std::memcpy(buf_.data(), session_, 10);
    for (int i = 0; i < 8; ++i)
      buf_[10 + i] = uint8_t(sequence >> (56 - 8 * i));
    count_ = 0;
  }
  // Appends a message; false (and nothing added) if the packet has no room for it.
  bool add(const uint8_t *message, uint16_t length) {
    if (buf_.size() + 2 + length > maxBytes_)
      return false;
    buf_.push_back(uint8_t(length >> 8));
    buf_.push_back(uint8_t(length));
    buf_.insert(buf_.end(), message, message + length);
    ++count_;
    return true;
  }
  // Writes the message count; the packet is then ready to send.
  void finish(uint16_t count) {
    buf_[18] = uint8_t(count >> 8);
    buf_[19] = uint8_t(count);
  }
  void finish() { finish(count_); }

  const uint8_t *data() const { return buf_.data(); }
  size_t size() const { return buf_.size(); }
  uint16_t count() const { return count_; }

 private:
  char session_[10];
  size_t maxBytes_;
  std::vector<uint8_t> buf_;
  uint16_t count_ = 0;
};

// Parses a packet and calls f(message, length) for each message. Returns false if the packet is
// truncated or its messages do not add up to its count.
template <class F>
bool parse(const uint8_t *p, size_t n, Header &h, F f) {
  if (n < kHeaderBytes)
    return false;
  std::memcpy(h.session, p, 10);
  h.sequence = itch::be64(p + 10);
  h.count = itch::be16(p + 18);
  if (h.count == kEndOfSession)
    return n == kHeaderBytes;
  size_t pos = kHeaderBytes;
  for (uint16_t i = 0; i < h.count; ++i) {
    if (pos + 2 > n)
      return false;
    uint16_t len = itch::be16(p + pos);
    if (pos + 2 + len > n)
      return false;
    f(p + pos + 2, len);
    pos += 2 + len;
  }
  return pos == n;
}

// Follows the sequence numbers of one session. For each packet it says how many leading messages were
// already seen (a duplicate or overlapping packet) and how many were missed before it (a gap).
class SequenceTracker {
 public:
  struct Verdict {
    uint64_t skip;  // leading messages of this packet to ignore
    uint64_t gap;   // messages lost before this packet
  };

  Verdict onPacket(uint64_t sequence, uint16_t count) {
    Verdict v{0, 0};
    if (sequence > next_) {
      v.gap = sequence - next_;
      gaps_ += v.gap;
    } else if (sequence < next_) {
      v.skip = std::min<uint64_t>(next_ - sequence, count);
      duplicates_ += v.skip;
    }
    if (sequence + count > next_)
      next_ = sequence + count;
    return v;
  }

  uint64_t next() const { return next_; }         // the next sequence number expected
  uint64_t gaps() const { return gaps_; }         // messages missed in total
  uint64_t duplicates() const { return duplicates_; }

 private:
  uint64_t next_ = 1;  // the publisher numbers a session's first message 1
  uint64_t gaps_ = 0;
  uint64_t duplicates_ = 0;
};

}  // namespace mold

#endif
