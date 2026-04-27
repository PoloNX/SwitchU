#pragma once
// ThreadPool — lightweight, reusable worker pool.
//
// Usage:
//     nxui::ThreadPool pool(2);                       // 2 workers
//     auto fut = pool.submit([]{ heavyWork(); });     // returns std::future<void>
//     // ... poll with fut.wait_for(0s) or block with fut.get() ...
//
// The pool shuts down cleanly in its destructor (waits for in-flight tasks).
#include <functional>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <cstddef>

namespace nxui {

class ThreadPool {
public:
    /// Create a pool with `numWorkers` threads.
    explicit ThreadPool(std::size_t numWorkers = 2) {
        m_workers.reserve(numWorkers);
        for (std::size_t i = 0; i < numWorkers; ++i) {
            m_workers.emplace_back([this]() { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& w : m_workers)
            if (w.joinable()) w.join();
    }

    // Non-copyable, non-movable.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Submit a task. Returns a future that resolves when the task completes.
    std::future<void> submit(std::function<void()> task) {
        auto promise = std::make_shared<std::promise<void>>();
        auto future  = promise->get_future();

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_queue.push([t = std::move(task), p = std::move(promise)]() {
                try {
                    t();
                    p->set_value();
                } catch (...) {
                    p->set_exception(std::current_exception());
                }
            });
        }
        m_cv.notify_one();
        return future;
    }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this]() { return m_stop || !m_queue.empty(); });
                if (m_stop && m_queue.empty()) return;
                task = std::move(m_queue.front());
                m_queue.pop();
            }
            task();
        }
    }

    std::vector<std::thread>            m_workers;
    std::queue<std::function<void()>>   m_queue;
    std::mutex                          m_mutex;
    std::condition_variable             m_cv;
    bool                                m_stop = false;
};

} // namespace nxui
