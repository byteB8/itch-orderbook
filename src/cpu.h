#ifndef CPU_H
#define CPU_H

#include <pthread.h>
#include <sched.h>

// Pins the calling thread to one logical CPU, so the scheduler does not move it (and its warm caches)
// elsewhere. A negative cpu leaves the thread where it is. Returns false if the kernel refused.
inline bool pinThisThread(int cpu) {
  if (cpu < 0)
    return true;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0;
}

#endif
