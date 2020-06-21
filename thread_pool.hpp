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
  bool TryPost(Handler&& handler);

  /**
   * @brief post Post job to thread pool.
   * @param handler Handler to be called from thread pool worker. It has
   * to be callable as 'handler()'.
   * @throw std::overflow_error if worker's queue is full.
   * @note All exceptions thrown by handler will be suppressed.
   */
  template <typename Handler>
  void Post(Handler&& handler);

 private:
  Worker<Task, Queue>& getWorker();

  std::vector<std::unique_ptr<Worker<Task, Queue>>> workers_;
  std::atomic<size_t> next_worker_id_;
};

/// Implementation

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::ThreadPoolImpl(
    const ThreadPoolOptions& options)
    : workers_(options.ThreadCount()), next_worker_id_(0) {
  for (auto& worker_ptr : workers_) {
    worker_ptr.reset(new Worker<Task, Queue>(options.queueSize()));
  }

  for (size_t i = 0; i < workers_.size(); ++i) {
    Worker<Task, Queue>* steal_donor =
        workers_[(i + 1) % workers_.size()].get();
    i % 2 == 0 ? workers_[i]->Start(i, kWorkerPriorityLow, steal_donor)
               : workers_[i]->Start(i, kWorkerPriorityHigh, steal_donor);
  }
}

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::ThreadPoolImpl(
    ThreadPoolImpl<Task, Queue>&& rhs) noexcept {
  *this = rhs;
}

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>::~ThreadPoolImpl() {
  for (auto& worker_ptr : workers_) {
    worker_ptr->Stop();
  }
}

template <typename Task, template <typename> class Queue>
inline ThreadPoolImpl<Task, Queue>& ThreadPoolImpl<Task, Queue>::operator=(
    ThreadPoolImpl<Task, Queue>&& rhs) noexcept {
  if (this != &rhs) {
    workers_ = std::move(rhs.workers_);
    next_worker_id_ = rhs.next_worker_id_.load();
  }
  return *this;
}

template <typename Task, template <typename> class Queue>
template <typename Handler>
inline bool ThreadPoolImpl<Task, Queue>::TryPost(Handler&& handler) {
  return getWorker().Post(std::forward<Handler>(handler));
}

template <typename Task, template <typename> class Queue>
template <typename Handler>
inline void ThreadPoolImpl<Task, Queue>::Post(Handler&& handler) {
  const auto ok = TryPost(std::forward<Handler>(handler));
  if (!ok) {
    throw std::runtime_error("thread pool queue is full");
  }
}

template <typename Task, template <typename> class Queue>
inline Worker<Task, Queue>& ThreadPoolImpl<Task, Queue>::getWorker() {
  size_t id = 0;
  if (id < workers_.size()) {
    id = next_worker_id_.fetch_add(1, std::memory_order_relaxed) %
         workers_.size();
  }

  return *workers_[id];
}
}
