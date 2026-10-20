#ifndef UDP_H
#define UDP_H

// UDP multicast on one local interface, loopback by default. The sender sets the multicast TTL to 0, so
// datagrams are delivered only to receivers on the same host and never leave it.

#include <netinet/in.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>

class MulticastSender {
 public:
  MulticastSender(const std::string &group, uint16_t port, const std::string &interface = "127.0.0.1");
  ~MulticastSender();
  MulticastSender(const MulticastSender &) = delete;
  MulticastSender &operator=(const MulticastSender &) = delete;

  bool send(const uint8_t *data, size_t size);

 private:
  int fd_ = -1;
  sockaddr_in dest_{};
};

// receive() does not block: it returns 0 when nothing is waiting, so the caller can poll in a loop.
class MulticastReceiver {
 public:
  MulticastReceiver(const std::string &group, uint16_t port, const std::string &interface = "127.0.0.1",
                    int bufferBytes = 8 << 20);
  ~MulticastReceiver();
  MulticastReceiver(const MulticastReceiver &) = delete;
  MulticastReceiver &operator=(const MulticastReceiver &) = delete;

  ssize_t receive(uint8_t *buf, size_t capacity);  // bytes received, 0 if none waiting, -1 on error
  int bufferBytes() const;                         // the receive buffer the kernel actually granted

 private:
  int fd_ = -1;
};

#endif
