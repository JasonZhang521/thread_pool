#pragma once

#include <algorithm>
#include <thread>

namespace tp {

// worker thread priority level
const static size_t kWorkerPriorityLow = 0;
const static size_t kWorkerPriorityHigh = 1;

/**
 * @brief The ThreadPoolOptions class provides creation options for
 * ThreadPool.
 */
class ThreadPoolOptions {
 public:
  /**
   * @brief ThreadPoolOptions Construct default options for thread pool.
   */
  ThreadPoolOptions();

  /**
   * @brief setThreadCount Set thread count.
   * @param count Number of threads to be created.
   */
  void SetThreadCount(size_t count);

  /**
   * @brief setQueueSize Set single worker queue size.
   * @param count Maximum length of queue of single worker.
   */
  void SetQueueSize(size_t size);

  /**
   * @brief threadCount Return thread count.
   */
  size_t ThreadCount() const;

  /**
   * @brief queueSize Return single worker queue size.
   */
  size_t QueueSize() const;

 private:
  size_t thread_count_;
  size_t queue_size_;
};

/// Implementation

inline ThreadPoolOptions::ThreadPoolOptions()
    : thread_count_(std::max<size_t>(1u, std::thread::hardware_concurrency())),
      queue_size_(1024u) {}

inline void ThreadPoolOptions::SetThreadCount(size_t count) {
  thread_count_ = std::max<size_t>(1u, count);
}

inline void ThreadPoolOptions::SetQueueSize(size_t size) {
  queue_size_ = std::max<size_t>(1u, size);
}

inline size_t ThreadPoolOptions::ThreadCount() const { return thread_count_; }

inline size_t ThreadPoolOptions::QueueSize() const { return queue_size_; }
}
