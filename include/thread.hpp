#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace aurora::chess
{

    class ThreadPool
    {
    public:
        explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency());
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        void resize(std::size_t thread_count);
        [[nodiscard]] std::size_t size() const noexcept;

        void parallel_for(std::size_t task_count, const std::function<void(std::size_t)>& task);
        void run_with_workers(const std::function<void(std::size_t)>& task);
        void wait_for_search_finished() const;

    private:
        class WorkerThread;

        mutable std::mutex resize_mutex_;
        std::vector<std::unique_ptr<WorkerThread>> workers_;
        std::size_t thread_count_{1};
    };

} // namespace aurora::chess
