#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
namespace flashforge {
class ThreadPool {
 public:
  explicit ThreadPool(std::size_t threads);
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  template<class F, class... Args> auto submit(F&& f, Args&&... args) {
    using R = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<R()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    auto result = task->get_future();
    { std::lock_guard lock(mutex_); if (stopping_) throw std::runtime_error("thread pool stopped");
      tasks_.emplace([task]{ (*task)(); }); }
    cv_.notify_one(); return result;
  }
 private:
  std::vector<std::thread> workers_; std::queue<std::function<void()>> tasks_;
  std::mutex mutex_; std::condition_variable cv_; bool stopping_{false};
};
}

