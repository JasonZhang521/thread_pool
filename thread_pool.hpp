#pragma once

#include <thread_pool/mpmc_bounded_queue.hpp>
#include <thread_pool/thread_pool_options.hpp>
#include <thread_pool/worker.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <vector>

namespace tp {
using TaskFuc = std::function<void()>;
template <typename Task, template <typename> class Queue>
class ThreadPoolImpl;
using ThreadPool = ThreadPoolImpl<TaskFuc, MPMCBoundedQueue>;
enum WorkerPriority { LOW = 0, HIGH = 1 };
/**
 * @brief The ThreadPool class implements thread pool pattern.
 * It is highly scalable and fast.
 * It is header only.
 * It implements both work-stealing and work-distribution balancing
 * startegies.
 * It implements cooperative scheduling strategy for tasks.
 */
template <typename Task, template <typename> class Queue>
class ThreadPoolImpl {
 public:
  /**
   * @brief ThreadPool Construct and start new thread pool.
   * @param options Creation options.
   */
  explicit ThreadPoolImpl(
      const ThreadPoolOptions& options = ThreadPoolOptions());

  /**
   * @brief Move ctor implementation.
   */
  ThreadPoolImpl(ThreadPoolImpl&& rhs) noexcept;

  /**
   * @brief ~ThreadPool Stop all workers and destroy thread pool.
   */
  ~ThreadPoolImpl();

  /**
   * @brief Move assignment implementaion.
   */
  ThreadPoolImpl& operator=(ThreadPoolImpl&& rhs) noexcept;

  /**
   * @brief post Try post job to thread pool.
   * @param handler Handler to be called from thread pool worker. It has
   * to be callable as 'handler()'.
   * @return 'true' on success, false otherwise.
   * @note All exceptions thrown by handler will be suppressed.
   */
  template <typename Handler>
  bool TryPost(Handler&& handler, WorkerPriority priority);

  /**
   * @brief AddTask AddTask job to thread pool.
   * @param handler Handler to be called from thread pool worker. It has
   * to be callable as 'handler()'.
   * @throw std::overflow_error if worker's queue is full.
   * @note All exceptions thrown by handler will be suppressed.
   */
  template <typename Handler>
  void AddTask(Handler&& handler);

  /**
 * @brief AddTask AddTask job to thread pool.
 * @param handler Handler to be called from thread pool worker. It has
 * to be callable as 'handler()'.
 * @param priority task priority
 * @throw std::overflow_error if worker's queue is full.
 * @note All exceptions thrown by handler will be suppressed.
 */
  template <typename Handler>
  void AddTask(Handler&& handler, WorkerPriority priority);

 private:
  Worker<Task, Queue>& getWorker(WorkerPriority priority);

  std::vector<std::unique_ptr<Worker<Task, Queue>>> default_workers_;
  std::vector<std::unique_ptr<Worker<Task, Queue>>> high_priority_workers_;
  std::atomic<size_t> next_worker_id_;
  std::atomic<size_t> next_high_priority_worker_id_;
};

/// Implementation

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::ThreadPoolImpl(
    const ThreadPoolOptions& options)
    : default_workers_(options.ThreadCount() - options.HighPrioritySize()),
      high_priority_workers_(options.HighPrioritySize()),
      next_worker_id_(0),
      next_high_priority_worker_id_(0) {
  for (auto& worker_ptr : default_workers_) {
    worker_ptr.reset(new Worker<Task, Queue>(options.queueSize()));
  }

  for (auto& worker_ptr : high_priority_workers_) {
    worker_ptr.reset(new Worker<Task, Queue>(options.queueSize()));
  }

  for (size_t i = 0; i < default_workers_.size(); ++i) {
    Worker<Task, Queue>* steal_donor =
        default_workers_[(i + 1) % default_workers_.size()].get();
    default_workers_[i]->Start(i, kWorkerPriorityLow, steal_donor);
  }

  for (size_t i = 0; i < high_priority_workers_.size(); ++i) {
    Worker<Task, Queue>* steal_donor =
        high_priority_workers_[(i + 1) % high_priority_workers_.size()].get();
    high_priority_workers_[i]->Start(i, kWorkerPriorityHigh, steal_donor);
  }
}

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::ThreadPoolImpl(
    ThreadPoolImpl<Task, Queue>&& rhs) noexcept {
  *this = rhs;
}

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::~ThreadPoolImpl() {
  for (auto& worker_ptr : default_workers_) {
    worker_ptr->Stop();
  }

  for (auto& worker_ptr : high_priority_workers_) {
    worker_ptr->Stop();
  }
}

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>& ThreadPoolImpl<Task, Queue>::operator=(
    ThreadPoolImpl<Task, Queue>&& rhs) noexcept {
  if (this != &rhs) {
    default_workers_ = std::move(rhs.default_workers_);
    high_priority_workers_ = std::move(rhs.high_priority_workers_);
    next_worker_id_ = rhs.next_worker_id_.load();
    next_high_priority_worker_id_ = rhs.next_high_priority_worker_id_.load();
  }
  return *this;
}

template <typename Task, template <typename> class Queue>
template <typename Handler>
inline bool ThreadPoolImpl<Task, Queue>::TryPost(Handler&& handler,
                                                 WorkerPriority priority) {
  return getWorker(priority).AddTask(std::forward<Handler>(handler));
}

template <typename Task, template <typename> class Queue>
template <typename Handler>
inline void ThreadPoolImpl<Task, Queue>::AddTask(Handler&& handler) {
  const auto ok = TryPost(std::forward<Handler>(handler), WorkerPriority::LOW);
  if (!ok) {
    throw std::runtime_error("thread pool queue is full");
  }
}

template <typename Task, template <typename> class Queue>
template <typename Handler>
inline void ThreadPoolImpl<Task, Queue>::AddTask(Handler&& handler,
                                                 WorkerPriority priority) {
  const auto ok = TryPost(std::forward<Handler>(handler), priority);
  if (!ok) {
    throw std::runtime_error("thread pool queue is full");
  }
}

template <typename Task, template <typename> class Queue>
inline Worker<Task, Queue>& ThreadPoolImpl<Task, Queue>::getWorker(
    WorkerPriority priority) {
  switch (priority) {
    case priority == WorkerPriority::LOW:
      size_t id = 0;
      if (id < default_workers_.size()) {
        id = next_worker_id_.fetch_add(1, std::memory_order_relaxed) %
             default_workers_.size();
      }

      return *default_workers_[id];
      break;

    case priority == WorkerPriority::HIGH:
      size_t id = 0;
      if (id < high_priority_workers_.size()) {
        id = next_high_priority_worker_id_.fetch_add(
                 1, std::memory_order_relaxed) %
             high_priority_workers_.size();
      }

      return *high_priority_workers_[id];
      break;

    default:
      break;
  }
}
}
