#ifndef ITCH_FILE_H
#define ITCH_FILE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

// An ITCH 5.0 file as NASDAQ publishes it: each message is preceded by its length (2 bytes,
// big-endian). The file is memory-mapped read-only. A cut-off message at the end (from a partial
// download) is ignored.
class ItchFile {
 public:
  explicit ItchFile(const std::string &path);  // throws std::runtime_error
  ~ItchFile();
  ItchFile(const ItchFile &) = delete;
  ItchFile &operator=(const ItchFile &) = delete;

  // Calls f(message, length) for each complete message, in order, and returns how many it was called
  // for. If f returns bool, returning false stops the loop after that message.
  template <class F>
  size_t forEach(F f) const {
    size_t count = 0;
    for (size_t pos = 0; pos + 2 <= size_;) {
      uint16_t len = uint16_t(data_[pos] << 8 | data_[pos + 1]);
      if (pos + 2 + len > size_)
        break;
      pos += 2 + len;
      ++count;
      if constexpr (std::is_same_v<std::invoke_result_t<F, const uint8_t *, uint16_t>, bool>) {
        if (!f(data_ + pos - len, len))
          break;
      } else {
        f(data_ + pos - len, len);
      }
    }
    return count;
  }

  const uint8_t *data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
};

#endif
