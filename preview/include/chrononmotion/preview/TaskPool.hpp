// ChrononMotion preview — deterministic-friendly worker pools.
//
// The motion core stays single-threaded and pure. This is the application-side
// executor: IO work (asset bytes, decode), compute work (frame/rig bake) and
// short async jobs (thumbnails, cache warm-up). It deliberately offers ordered
// map/reduce helpers so "parallel" never means "nondeterministic output order".

#ifndef CHRONONMOTION_PREVIEW_TASKPOOL_HPP
#define CHRONONMOTION_PREVIEW_TASKPOOL_HPP

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace chrononmotion::preview {

    class TaskPool {
    public:
        explicit TaskPool(std::size_t workers = 1, std::string name = {})
            : name_(std::move(name)) {
            if (workers == 0) workers = 1;
            workers_.reserve(workers);
            for (std::size_t i = 0; i < workers; ++i) {
                workers_.emplace_back([this] { workerLoop(); });
            }
        }

        ~TaskPool() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
                // Drop queued work: a destroyed pool cannot make progress on it.
                while (!tasks_.empty()) tasks_.pop_front();
            }
            cv_.notify_all();
            for (auto& worker : workers_) {
                if (worker.joinable()) worker.join();
            }
        }

        TaskPool(const TaskPool&) = delete;
        TaskPool& operator=(const TaskPool&) = delete;
        TaskPool(TaskPool&&) = delete;
        TaskPool& operator=(TaskPool&&) = delete;

        [[nodiscard]] const std::string& name() const { return name_; }
        [[nodiscard]] std::size_t workerCount() const { return workers_.size(); }

        template<class F>
        auto submit(F&& fn) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
            using Result = std::invoke_result_t<std::decay_t<F>>;
            auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(fn));
            std::future<Result> future = task->get_future();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) {
                    throw std::runtime_error("TaskPool::submit: pool is stopping");
                }
                tasks_.emplace_back([task] { (*task)(); });
            }
            cv_.notify_one();
            return future;
        }

        /// Run `fn(index)` for every index in [0, count), blocking until done.
        /// The caller participates, so a one-worker pool still makes progress
        /// when `parallelFor` is called from that worker.
        template<class F>
        void parallelFor(std::size_t count, F&& fn) {
            if (count == 0) return;
            if (count == 1 || workers_.size() == 1) {
                for (std::size_t i = 0; i < count; ++i) fn(i);
                return;
            }

            auto next = std::make_shared<std::atomic<std::size_t>>(0);
            const std::size_t jobs = std::min(workers_.size(), count);
            std::vector<std::future<void>> futures;
            futures.reserve(jobs);

            auto work = [next, count, &fn] {
                for (;;) {
                    const std::size_t i = next->fetch_add(1, std::memory_order_relaxed);
                    if (i >= count) return;
                    fn(i);
                }
            };

            // If this call is made from a worker, the caller can execute one
            // slice inline. With one worker there is no queued slice (handled
            // above), so this can never deadlock.
            for (std::size_t i = 0; i < jobs; ++i) {
                futures.push_back(submit(work));
            }
            work();

            // Rethrow the first error only after every slice has finished, so a
            // failed worker cannot leave the pool observing a half-run pass.
            std::exception_ptr first;
            for (auto& future : futures) {
                try {
                    future.get();
                } catch (...) {
                    if (!first) first = std::current_exception();
                }
            }
            if (first) std::rethrow_exception(first);
        }

        /// Index-ordered result collection. `results[i]` is always `fn(i)` for
        /// the same i, independent of scheduling.
        template<class T, class F>
        std::vector<T> parallelMap(std::size_t count, F&& fn) {
            std::vector<T> results(count);
            parallelFor(count, [&results, &fn](std::size_t i) {
                results[i] = fn(i);
            });
            return results;
        }

        /// Chunked map + left-to-right reduce. Each chunk folds its indices in
        /// ascending order; the final fold visits chunks in ascending order.
        /// The result is therefore identical to the serial left fold whenever
        /// `map` is pure, even though chunks run concurrently.
        template<class T, class Map, class Reduce>
        T parallelReduce(std::size_t count, T identity, Map&& map, Reduce&& reduce) {
            if (count == 0) return identity;
            const std::size_t chunks = std::max<std::size_t>(
                    1, std::min<std::size_t>(workers_.size() * 4, count));
            const std::size_t chunkSize = (count + chunks - 1) / chunks;
            const std::size_t actualChunks = (count + chunkSize - 1) / chunkSize;

            auto partials = parallelMap<T>(actualChunks, [&](std::size_t chunk) {
                T partial = identity;
                const std::size_t begin = chunk * chunkSize;
                const std::size_t end = std::min(count, begin + chunkSize);
                for (std::size_t i = begin; i < end; ++i) {
                    partial = reduce(std::move(partial), map(i));
                }
                return partial;
            });

            T result = identity;
            for (auto& partial : partials) {
                result = reduce(std::move(result), std::move(partial));
            }
            return result;
        }

        /// Wait for the queue and every running task to drain. Tasks submitted
        /// while waiting are included; this is a quiescence barrier, not a
        /// snapshot of the current queue.
        void waitIdle() {
            std::unique_lock<std::mutex> lock(mutex_);
            idleCv_.wait(lock, [this] { return tasks_.empty() && active_ == 0; });
        }

        [[nodiscard]] bool stopping() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return stopping_;
        }

    private:
        void workerLoop() {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                    if (stopping_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                    ++active_;
                }

                try {
                    task();
                } catch (...) {
                    // packaged_task captures the exception in its future. A
                    // plain task's exception has nowhere to go; swallowing it
                    // keeps the worker alive, and submit() users always get a
                    // packaged future.
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    --active_;
                    if (tasks_.empty() && active_ == 0) idleCv_.notify_all();
                }
            }
        }

        std::string name_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::condition_variable idleCv_;
        std::deque<std::function<void()>> tasks_;
        std::vector<std::thread> workers_;
        std::size_t active_{0};
        bool stopping_{false};
    };

    /// The three Bevy-style lanes, kept as separate pools so a long asset read
    /// cannot starve frame bake and a preview job cannot starve either.
    class TaskPools {
    public:
        explicit TaskPools(std::size_t ioThreads = 1,
                           std::size_t computeThreads = 0,
                           std::size_t asyncThreads = 2)
            : io(std::max<std::size_t>(1, ioThreads), "io"),
              compute(computeThreads == 0
                              ? std::max<std::size_t>(1, std::thread::hardware_concurrency() > 1
                                                              ? std::thread::hardware_concurrency() - 1
                                                              : 1)
                              : computeThreads,
                      "compute"),
              async(std::max<std::size_t>(1, asyncThreads), "async") {}

        TaskPool io;
        TaskPool compute;
        TaskPool async;

        void waitIdle() {
            io.waitIdle();
            compute.waitIdle();
            async.waitIdle();
        }
    };

}// namespace chrononmotion::preview

#endif//CHRONONMOTION_PREVIEW_TASKPOOL_HPP
