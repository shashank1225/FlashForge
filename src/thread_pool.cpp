#include "thread_pool.h"
namespace flashforge {
ThreadPool::ThreadPool(std::size_t n){if(n==0)n=1;for(std::size_t i=0;i<n;++i)workers_.emplace_back([this]{for(;;){std::function<void()> task;{std::unique_lock lock(mutex_);cv_.wait(lock,[this]{return stopping_||!tasks_.empty();});if(stopping_&&tasks_.empty())return;task=std::move(tasks_.front());tasks_.pop();}task();}});}
ThreadPool::~ThreadPool(){{std::lock_guard lock(mutex_);stopping_=true;}cv_.notify_all();for(auto& w:workers_)w.join();}
}

