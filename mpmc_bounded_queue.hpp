#pragma once

#include <atomic>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace tp {

/**
 * @brief The MPMCBoundedQueue class implements bounded
 * multi-producers/multi-consumers lock-free queue.
 * Doesn't accept non-movable types as T.
 * Inspired by Dmitry Vyukov's mpmc queue.
 * http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
 */
template <typename T>
class MPMCBoundedQueue {
  static_assert(std::is_move_constructible<T>::value,
                "Should be of movable type");

 public:
  /**
   * @brief MPMCBoundedQueue Constructor.
   * @param size Power of 2 number - queue length.
   * @throws std::invalid_argument if size is bad.
   */
  explicit MPMCBoundedQueue(size_t size);

  /**
   * @brief Move ctor implementation.
   */
  MPMCBoundedQueue(MPMCBoundedQueue&& rhs) noexcept;

  /**
   * @brief Move assignment implementaion.
   */
  MPMCBoundedQueue& operator=(MPMCBoundedQueue&& rhs) noexcept;

  /**
   * @brief push Push data to queue.
   * @param data Data to be pushed.
   * @return true on success.
   */
  template <typename U>
  bool Push(U&& data);

  /**
   * @brief pop Pop data from queue.
   * @param data Place to store popped data.
   * @return true on sucess.
   */
  bool Pop(T& data);

 private:
  struct Cell {
    std::atomic<size_t> sequence_;
    T data_;

    Cell() = default;

    Cell(const Cell&) = delete;
    Cell& operator=(const Cell&) = delete;

    Cell(Cell&& rhs)
        : sequence_(rhs.sequence_.load()), data_(std::move(rhs.data_)) {}

    Cell& operator=(Cell&& rhs) {
      sequence_ = rhs.sequence_.load();
      data_ = std::move(rhs.data_);

      return *this;
    }
  };

 private:
  typedef char Cacheline[64];

  Cacheline pad0_;
  std::vector<Cell> buffer_;
  /* const */ size_t buffer_mask_;
  Cacheline pad1_;
  std::atomic<size_t> enqueue_pos_;
  Cacheline pad2_;
  std::atomic<size_t> dequeue_pos_;
  Cacheline pad3_;
};

/// Implementation

template <typename T>
inline MPMCBoundedQueue<T>::MPMCBoundedQueue(size_t size)
    : buffer_(size), buffer_mask_(size - 1), enqueue_pos_(0), dequeue_pos_(0) {
  bool size_is_power_of_2 = (size >= 2) && ((size & (size - 1)) == 0);
  if (!size_is_power_of_2) {
    throw std::invalid_argument("buffer size should be a power of 2");
  }

  for (size_t i = 0; i < size; ++i) {
    buffer_[i].sequence_ = i;
  }
}

template <typename T>
inline MPMCBoundedQueue<T>::MPMCBoundedQueue(MPMCBoundedQueue&& rhs) noexcept {
  *this = rhs;
}

template <typename T>
inline MPMCBoundedQueue<T>& MPMCBoundedQueue<T>::operator=(
    MPMCBoundedQueue&& rhs) noexcept {
  if (this != &rhs) {
    buffer_ = std::move(rhs.buffer_);
    buffer_mask_ = std::move(rhs.buffer_mask_);
    enqueue_pos_ = rhs.enqueue_pos_.load();
    dequeue_pos_ = rhs.dequeue_pos_.load();
  }
  return *this;
}

template <typename T>
template <typename U>
inline bool MPMCBoundedQueue<T>::Push(U&& data) {
  Cell* cell;
  size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
  for (;;) {
    cell = &buffer_[pos & buffer_mask_];
    size_t seq = cell->sequence_.load(std::memory_order_acquire);
    intptr_t dif = (intptr_t)seq - (intptr_t)pos;
    if (dif == 0) {
      if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                             std::memory_order_relaxed)) {
        break;
      }
    } else if (dif < 0) {
      return false;
    } else {
      pos = enqueue_pos_.load(std::memory_order_relaxed);
    }
  }

  cell->data = std::forward<U>(data);

  cell->sequence_.store(pos + 1, std::memory_order_release);

  return true;
}

template <typename T>
inline bool MPMCBoundedQueue<T>::Pop(T& data) {
  Cell* cell;
  size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
  for (;;) {
    cell = &buffer_[pos & buffer_mask_];
    size_t seq = cell->sequence_.load(std::memory_order_acquire);
    intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
    if (dif == 0) {
      if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                             std::memory_order_relaxed)) {
        break;
      }
    } else if (dif < 0) {
      return false;
    } else {
      pos = dequeue_pos_.load(std::memory_order_relaxed);
    }
  }

  data = std::move(cell->data);

  cell->sequence_.store(pos + buffer_mask_ + 1, std::memory_order_release);

  return true;
}
}
