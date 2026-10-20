#include "itch_file.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

ItchFile::ItchFile(const std::string &path) {
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0)
    throw std::runtime_error("cannot open " + path + ": " + std::strerror(errno));
  struct stat st {};
  if (::fstat(fd, &st) != 0) {
    ::close(fd);
    throw std::runtime_error("cannot stat " + path);
  }
  size_ = size_t(st.st_size);
  if (size_ > 0) {
    void *p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
      ::close(fd);
      throw std::runtime_error("cannot map " + path + ": " + std::strerror(errno));
    }
    ::madvise(p, size_, MADV_SEQUENTIAL);
    data_ = static_cast<const uint8_t *>(p);
  }
  ::close(fd);  // the mapping stays valid
}

ItchFile::~ItchFile() {
  if (data_)
    ::munmap(const_cast<uint8_t *>(data_), size_);
}
