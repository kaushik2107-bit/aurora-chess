#include "thread.hpp"

#include <algorithm>
#include <atomic>

namespace aurora::chess
{

    class ThreadPool::WorkerThread
    {
    public:
        WorkerThread() : thread_(&WorkerThread::idle_loop, this)
        {
            wait_for_search_finished();
        }

        ~WorkerThread()
        {
            wait_for_search_finished();
            {
                std::lock_guard lock{mutex_};
                exit_ = true;
                searching_ = true;
            }
            cv_.notify_one();
            thread_.join();
        }

        WorkerThread(const WorkerThread&) = delete;
        WorkerThread& operator=(const WorkerThread&) = delete;

        void run_custom_job(std::function<void()> job)
        {
            {
                std::unique_lock lock{mutex_};
                cv_.wait(lock, [&] { return !searching_; });
                job_ = std::move(job);
                searching_ = true;
            }
            cv_.notify_one();
        }

        void wait_for_search_finished() const
        {
            std::unique_lock lock{mutex_};
            cv_.wait(lock, [&] { return !searching_; });
        }

    private:
        void idle_loop()
        {
            while (true)
            {
                std::function<void()> job;
                {
                    std::unique_lock lock{mutex_};
                    searching_ = false;
                    cv_.notify_all();
                    cv_.wait(lock, [&] { return searching_; });
                    if (exit_)
                    {
                        return;
                    }
                    job = std::move(job_);
                }

                if (job)
                {
                    job();
                }
            }
        }

        mutable std::mutex mutex_;
        mutable std::condition_variable cv_;
        std::function<void()> job_;
        bool exit_{false};
        bool searching_{true};
        std::thread thread_;
    };

    ThreadPool::ThreadPool(std::size_t thread_count)
    {
        resize(thread_count);
    }

    ThreadPool::~ThreadPool()
    {
        std::lock_guard lock{resize_mutex_};
        workers_.clear();
    }

    void ThreadPool::resize(std::size_t thread_count)
    {
        thread_count = std::max<std::size_t>(1, thread_count);
        std::lock_guard lock{resize_mutex_};
        for (const auto& worker : workers_)
        {
            worker->wait_for_search_finished();
        }

        thread_count_ = thread_count;
        const std::size_t helper_count = thread_count_ - 1;
        while (workers_.size() > helper_count)
        {
            workers_.pop_back();
        }
        while (workers_.size() < helper_count)
        {
            workers_.push_back(std::make_unique<WorkerThread>());
        }
    }

    std::size_t ThreadPool::size() const noexcept
    {
        return thread_count_;
    }

    void ThreadPool::parallel_for(std::size_t task_count, const std::function<void(std::size_t)>& task)
    {
        if (task_count == 0)
        {
            return;
        }

        std::unique_lock resize_lock{resize_mutex_};
        std::atomic_size_t next_task{0};
        auto run_tasks = [&]
        {
            while (true)
            {
                const std::size_t index = next_task.fetch_add(1, std::memory_order_relaxed);
                if (index >= task_count)
                {
                    break;
                }
                task(index);
            }
        };

        for (const auto& worker : workers_)
        {
            worker->run_custom_job(run_tasks);
        }

        run_tasks();

        for (const auto& worker : workers_)
        {
            worker->wait_for_search_finished();
        }
    }

    void ThreadPool::wait_for_search_finished() const
    {
        std::lock_guard lock{resize_mutex_};
        for (const auto& worker : workers_)
        {
            worker->wait_for_search_finished();
        }
    }

} // namespace aurora::chess
