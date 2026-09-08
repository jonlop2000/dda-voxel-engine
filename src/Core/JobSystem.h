#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class JobSystem
{
public:
    void start(uint32_t threadCount);
    void stop();
    void enqueue(std::function<void()> job);
    void waitIdle();
    size_t pendingCount() const;

private:
    void workerLoop();

    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::condition_variable idleCv_{};
    std::deque<std::function<void()>> queue_{};
    std::vector<std::thread> workers_{};
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> activeJobs_{0};
};
