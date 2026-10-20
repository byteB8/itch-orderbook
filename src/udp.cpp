#include "udp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace {

in_addr address(const std::string &text) {
  in_addr a{};
  if (inet_pton(AF_INET, text.c_str(), &a) != 1)
    throw std::runtime_error("not an IPv4 address: " + text);
  return a;
}

void check(int rc, const char *what) {
  if (rc != 0)
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

}  // namespace

MulticastSender::MulticastSender(const std::string &group, uint16_t port, const std::string &interface) {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0)
    throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
  try {
    in_addr iface = address(interface);
    unsigned char ttl = 0, loop = 1;
    check(::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof iface), "IP_MULTICAST_IF");
    check(::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl), "IP_MULTICAST_TTL");
    check(::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop), "IP_MULTICAST_LOOP");
    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(port);
    dest_.sin_addr = address(group);
  } catch (...) {
    ::close(fd_);  // the destructor does not run when a constructor throws
    throw;
  }
}

MulticastSender::~MulticastSender() {
  if (fd_ >= 0)
    ::close(fd_);
}

bool MulticastSender::send(const uint8_t *data, size_t size) {
  return ::sendto(fd_, data, size, 0, reinterpret_cast<const sockaddr *>(&dest_), sizeof dest_) == ssize_t(size);
}

MulticastReceiver::MulticastReceiver(const std::string &group, uint16_t port, const std::string &interface,
                                     int bufferBytes) {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0)
    throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
  try {
    int one = 1;
    check(::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one), "SO_REUSEADDR");
    // the kernel caps this at net.core.rmem_max (doubled) for unprivileged processes
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bufferBytes, sizeof bufferBytes);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr = address(group);  // bound to the group: only its datagrams arrive on this socket
    check(::bind(fd_, reinterpret_cast<const sockaddr *>(&local), sizeof local), "bind");
    ip_mreq membership{};
    membership.imr_multiaddr = address(group);
    membership.imr_interface = address(interface);
    check(::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof membership), "IP_ADD_MEMBERSHIP");
    check(::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK), "O_NONBLOCK");
  } catch (...) {
    ::close(fd_);
    throw;
  }
}

MulticastReceiver::~MulticastReceiver() {
  if (fd_ >= 0)
    ::close(fd_);
}

ssize_t MulticastReceiver::receive(uint8_t *buf, size_t capacity) {
  ssize_t n = ::recv(fd_, buf, capacity, 0);
  if (n >= 0)
    return n;
  return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
}

int MulticastReceiver::bufferBytes() const {
  int bytes = 0;
  socklen_t len = sizeof bytes;
  ::getsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bytes, &len);
  return bytes;
}
