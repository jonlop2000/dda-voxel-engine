#include "Core/JobSystem.h"

void JobSystem::start(uint32_t threadCount)
{
    stop();
    if (threadCount == 0)
    {
        threadCount = 1;
    }

    running_.store(true);
    workers_.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

void JobSystem::stop()
{
    running_.store(false);
    cv_.notify_all();
    for (auto& worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    workers_.clear();

    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    activeJobs_.store(0);
}

void JobSystem::enqueue(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void JobSystem::waitIdle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idleCv_.wait(lock, [this]() { return queue_.empty() && activeJobs_.load() == 0; });
}

size_t JobSystem::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void JobSystem::workerLoop()
{
    while (true)
    {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return !running_.load() || !queue_.empty(); });
            if (!running_.load() && queue_.empty())
            {
                break;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
            activeJobs_.fetch_add(1);
        }

        job();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeJobs_.fetch_sub(1);
            if (queue_.empty() && activeJobs_.load() == 0)
            {
                idleCv_.notify_all();
            }
        }
    }
}
