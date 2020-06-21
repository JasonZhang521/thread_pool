#pragma once

#include <atomic>
#include <thread>

#define THREAD_PRIORITY_LEVEL_HIGH 99

namespace tp {

/**
 * @brief The Worker class owns task queue and executing thread.
 * In thread it tries to pop task from queue. If queue is empty then it tries
 * to steal task from the sibling worker. If steal was unsuccessful then spins
 * with one millisecond delay.
 */
template <typename Task, template <typename> class Queue>
class Worker {
 public:
  /**
   * @brief Worker Constructor.
   * @param queue_size Length of undelaying task queue.
   */
  explicit Worker(size_t queue_size);

  /**
   * @brief Move ctor implementation.
   */
  Worker(Worker&& rhs) noexcept;

  /**
   * @brief Move assignment implementaion.
   */
  Worker& operator=(Worker&& rhs) noexcept;

  /**
   * @brief start Create the executing thread and start tasks execution.
   * @param id Worker ID.
   * @param steal_donor Sibling worker to steal task from it.
   */
  void Start(size_t id, size_t priority, Worker* steal_donor);

  /**
   * @brief stop Stop all worker's thread and stealing activity.
   * Waits until the executing thread became finished.
   */
  void Stop();

  /**
   * @brief post Post task to queue.
   * @param handler Handler to be executed in executing thread.
   * @return true on success.
   */
  template <typename Handler>
  bool Post(Handler&& handler);

  /**
   * @brief steal Steal one task from this worker queue.
   * @param task Place for stealed task to be stored.
   * @return true on success.
   */
  bool Steal(Task& task);

  /**
   * @brief getWorkerIdForCurrentThread Return worker ID associated with
   * current thread if exists.
   * @return Worker ID.
   */
  static size_t GetWorkerIdForCurrentThread();

 private:
  /**
   * @brief threadFunc Executing thread function.
   * @param id Worker ID to be associated with this thread.
   * @param pritority Worker thread pritority, 0 for SCHED_OTHER , 1 for
   * SCHED_RR scheduling policy.
   * @param steal_donor Sibling worker to steal task from it.
   */
  void threadFunc(size_t id, size_t pritority, Worker* steal_donor);

  /**
   * @brief set thread priority.
   * @param percent thread priority level.
   * @return true on success.
   */
  bool setPriority(float percent);

  Queue<Task> queue_;
  std::atomic<bool> running_flag_;
  std::thread thread_;
  size_t priority_;
};

/// Implementation

namespace detail {
inline size_t* thread_id() {
  static thread_local size_t tss_id = -1u;
  return &tss_id;
}
}

template <typename Task, template <typename> class Queue>
inline Worker<Task, Queue>::Worker(size_t queue_size)
    : queue_(queue_size), running_flag_(true) {}

template <typename Task, template <typename> class Queue>
inline Worker<Task, Queue>::Worker(Worker&& rhs) noexcept {
  *this = rhs;
}

template <typename Task, template <typename> class Queue>
inline Worker<Task, Queue>& Worker<Task, Queue>::operator=(
    Worker&& rhs) noexcept {
  if (this != &rhs) {
    queue_ = std::move(rhs.queue_);
    running_flag_ = rhs.running_flag_.load();
    thread_ = std::move(rhs.thread_);
  }
  return *this;
}

template <typename Task, template <typename> class Queue>
inline void Worker<Task, Queue>::Stop() {
  running_flag_.store(false, std::memory_order_relaxed);
  thread_.join();
}

template <typename Task, template <typename> class Queue>
inline void Worker<Task, Queue>::Start(size_t id, size_t pritority,
                                       Worker* steal_donor) {
  thread_ = std::thread(&Worker<Task, Queue>::threadFunc, this, id, pritority,
                        steal_donor);
}

template <typename Task, template <typename> class Queue>
inline size_t Worker<Task, Queue>::GetWorkerIdForCurrentThread() {
  return *detail::thread_id();
}

template <typename Task, template <typename> class Queue>
template <typename Handler>
inline bool Worker<Task, Queue>::Post(Handler&& handler) {
  return queue_.Push(std::forward<Handler>(handler));
}

template <typename Task, template <typename> class Queue>
inline bool Worker<Task, Queue>::Steal(Task& task) {
  return queue_.Pop(task);
}

template <typename Task, template <typename> class Queue>
inline void Worker<Task, Queue>::threadFunc(size_t id, size_t pritority,
                                            Worker* steal_donor) {
  *detail::thread_id() = id;

  Worker<Task,Queue>::setPriority(std::static_cast<float>(pritority));

  Task handler;

  while (running_flag_.load(std::memory_order_relaxed)) {
    if (queue_.Pop(handler) || steal_donor->Steal(handler)) {
      try {
        handler();
      } catch (...) {
        // suppress all exceptions
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

template <typename Task, template <typename> class Queue>
inline bool Worker<Task, Queue>::setPriority(float percent) {
  struct sched_param param;
  int thread_policy = -1;
  pthread_t thread_id = pthread_self();
  if (pthread_getschedparam(thread_id, &thread_policy, &param) != 0) {
    return false;
  }

  if (percent < 0.0001)
    thread_policy = SCHED_OTHER;
  else
    thread_policy = SCHED_RR;

  int thread_min_priority = sched_get_priority_min(thread_policy);
  int thread_max_priority = sched_get_priority_max(thread_policy);

  param.sched_priority =
      (int)((thread_max_priority - thread_min_priority) * percent / 100 +
            thread_min_priority);
  if (pthread_setschedparam(thread_id, thread_policy, &param) != 0) {
    return false;
  }
  return true;
}
}
