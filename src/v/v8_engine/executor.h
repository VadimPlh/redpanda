/*
 * Copyright 2021 Vectorized, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/vectorizedio/redpanda/blob/master/licenses/rcl.md
 */

#pragma once

#include "seastarx.h"

#include <seastar/core/abort_source.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/lowres_clock.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>

#include <boost/lockfree/queue.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <semaphore.h>

namespace v8_engine {

namespace internal {

// This class is used for notify seastar thread when executor thread will
// complete task
class condition {
public:
    condition();

    // Run this function in seastar thread.
    ss::future<> wait();

    // Run this function in executor thread
    void notify();

private:
    ss::file_desc _file_desc;
    int _fd;
    ss::pollable_fd _pollable_fd;
    eventfd_t _event{0};
};

// Abstract class for executor task
struct work_item {
    virtual ~work_item() {}
    virtual void process() = 0;
};

// This class implement task for executor. Contains promise logic
template<typename Func>
struct task final : work_item {
    explicit task(Func&& f)
      : _func(std::move(f)) {}

    // Process task from executor in executor thread and set state in ss::future
    void process() override {
        try {
            _func();
            _state.set(true);
        } catch (...) {
            _state.set_exception(std::current_exception());
        }
        _on_done.notify();
    }

    // Get future for seastar thread for wating when executor thread will
    // complete task
    ss::future<> get_future() {
        return _on_done.wait().then([this] {
            if (_state.failed()) {
                return ss::make_exception_future<>(
                  std::move(_state).get_exception());
            } else {
                return ss::make_ready_future<>();
            }
        });
    }

    Func _func;
    ss::future_state<bool> _state;
    condition _on_done;
};

// This class implement wrapper for ss::gate for using in ss::sharded
class local_gate {
public:
    ss::future<> stop();

    ss::gate _gate;
};

// This class implement wrapper for ss::abort_source for using in ss::sharded.
class local_abort_source {
public:
    ss::future<> stop();

    ss::abort_source _abort_source;
};

// This class implement semaphore with exponential backoff.
// TODO: mybe use something with fairness guarantee
class semaphore {
public:
    explicit semaphore(size_t free_slots);

    ss::future<> start();
    ss::future<> stop();

    // Try to acquire semaphore and sleep if semaphore does not contain free
    // slots
    ss::future<> acquire();

    // Release sempahore
    void release();

private:
    std::atomic<size_t> _free_slots;

    static constexpr ss::lowres_clock::duration _max_wait_time_ms{100};

    // _abort_sources is used for abort sleep if semaphore is stoped
    ss::sharded<local_abort_source> _abort_sources;
    std::atomic<bool> _is_closed{false};
};

} // namespace internal

// This class implement thread pool with std::thread for runing v8 script. It
// uses boost::lockfree_queue for storing task. Queue task stores #thread_count
// tasks. Because we need to set watchdog for timeout v8 script only when
// executor can execute script without wating. semaphore is used for controle
// does executor have free thread.
class executor {
public:
    executor(size_t threads_num, uint8_t cpu_id);

    executor(const executor& other) = delete;
    executor& operator=(const executor& other) = delete;
    executor(executor&& other) = delete;
    executor& operator=(executor&& other) = delete;

    ~executor();

    // Start ss::sharded for ss::gate and ss::abort_source
    ss::future<> start();

    // Stop executor. Frst of all we need to stop semaphore for cancel waitnig
    // tasks. After that we need complete all task in queue and finally stop
    // std::threads
    ss::future<> stop();

    bool is_stopping() const;

    /// Submit new task in executor.
    ///
    /// \param start_func is used for set watchdog
    /// \param func_for_executor is v8 script
    template<typename StartFunc, typename FuncForExecutor>
    auto submit(StartFunc&& start_func, FuncForExecutor&& func_for_executor) {
        return ss::with_gate(
          _gates.local()._gate,
          [this,
           start_func = std::move(start_func),
           func_for_executor = std::move(func_for_executor)]() mutable {
              return _sem.acquire()
                .then(
                  [this,
                   start_func = std::move(start_func),
                   func_for_executor = std::move(func_for_executor)]() mutable {
                      auto new_task
                        = std::make_unique<internal::task<FuncForExecutor>>(
                          std::move(func_for_executor));
                      auto fut = new_task->get_future();
                      _tasks.push(new_task.get());
                      _has_task_cv.notify_one();
                      start_func(); // Rearm watchdog for srcipt
                      return fut.finally([new_task = std::move(new_task)] {});
                  })
                .finally([this] { _sem.release(); });
          });
    }

private:
    // Do we need to pin all thread for one core?
    void pin(unsigned cpu_id);

    // Main loop for threads in executor
    void loop();

    std::atomic<bool> _is_stopped{false};

    std::mutex mutex;
    std::condition_variable _has_task_cv;
    std::vector<std::thread> _threads; // maybe use ss::alien_thread

    ss::sharded<internal::local_gate> _gates;
    internal::semaphore _sem;
    boost::lockfree::queue<internal::work_item*> _tasks;
};

} // namespace v8_engine
