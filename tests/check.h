#ifndef CHECK_H
#define CHECK_H

// Minimal test helpers. CHECK and CHECK_EQ print and count failures; a test program
// returns report() from main, which is non-zero if any check failed.
#include <iostream>

inline int checksRun = 0;
inline int checksFailed = 0;

#define CHECK(cond)                                                                \
  do {                                                                             \
    ++checksRun;                                                                   \
    if (!(cond)) {                                                                 \
      ++checksFailed;                                                              \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n";     \
    }                                                                              \
  } while (0)

#define CHECK_EQ(a, b)                                                             \
  do {                                                                             \
    ++checksRun;                                                                   \
    const auto &va = (a);                                                          \
    const auto &vb = (b);                                                          \
    if (!(va == vb)) {                                                             \
      ++checksFailed;                                                              \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #a ", " #b ")"       \
                << " failed: " << va << " vs " << vb << "\n";                      \
    }                                                                              \
  } while (0)

inline int report(const char *name) {
  std::cout << name << ": " << checksRun - checksFailed << "/" << checksRun << " checks passed\n";
  return checksFailed == 0 ? 0 : 1;
}

#endif
