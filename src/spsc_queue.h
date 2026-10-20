#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

// A bounded queue for exactly one producer thread and one consumer thread, without locks.
//
// The producer writes a slot, then publishes it by advancing tail with a release store; the consumer
// loads tail with acquire, so it also sees what was written to the slot. In the other direction, the
// consumer frees a slot by advancing head with release, and the producer loads head with acquire before it
// overwrites that slot. Each side's index sits on its own cache line with that side's private copy of the
// other index, so a thread reads the other's line only when the queue looks full (producer) or empty
// (consumer), not on every operation.

#include <array>
#include <atomic>
#include <cstddef>

template <class T, size_t Capacity>
class SpscQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

 public:
  // Producer thread only. Returns false if the queue is full.
  bool tryPush(const T &value) {
    size_t tail = producer_.tail.load(std::memory_order_relaxed);
    if (tail - producer_.headCopy == Capacity) {
      producer_.headCopy = consumer_.head.load(std::memory_order_acquire);
      if (tail - producer_.headCopy == Capacity)
        return false;
    }
    slots_[tail & (Capacity - 1)] = value;
    producer_.tail.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Consumer thread only. Returns false if the queue is empty.
  bool tryPop(T &out) {
    size_t head = consumer_.head.load(std::memory_order_relaxed);
    if (head == consumer_.tailCopy) {
      consumer_.tailCopy = producer_.tail.load(std::memory_order_acquire);
      if (head == consumer_.tailCopy)
        return false;
    }
    out = slots_[head & (Capacity - 1)];
    consumer_.head.store(head + 1, std::memory_order_release);
    return true;
  }

 private:
  struct alignas(64) ConsumerSide {
    std::atomic<size_t> head{0};  // next slot to read; written by the consumer
    size_t tailCopy = 0;          // the consumer's last view of tail
  };
  struct alignas(64) ProducerSide {
    std::atomic<size_t> tail{0};  // next slot to write; written by the producer
    size_t headCopy = 0;          // the producer's last view of head
  };

  ConsumerSide consumer_;
  ProducerSide producer_;
  alignas(64) std::array<T, Capacity> slots_;
};

#endif
