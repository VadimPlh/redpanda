/*
 * Copyright 2021 Vectorized, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/vectorizedio/redpanda/blob/master/licenses/rcl.md
 */

#include "v8_engine/executor.h"

#include "seastarx.h"

#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/later.hh>

namespace v8_engine {

namespace internal {

// condition

condition::condition()
  : _file_desc{ss::file_desc::eventfd(0, 0)}
  , _fd(_file_desc.get())
  , _pollable_fd{std::move(_file_desc)} {}

ss::future<> condition::wait() {
    return _pollable_fd
      .read_some(reinterpret_cast<char*>(&_event), sizeof(_event))
      .then([](size_t) { return ss::make_ready_future<>(); });
}

void condition::notify() { ::eventfd_write(_fd, 1); }

// local_gate

ss::future<> local_gate::stop() { return _gate.close(); }

// local_abort_source

ss::future<> local_abort_source::stop() {
    _abort_source.request_abort();
    return ss::now();
}

// semaphore

semaphore::semaphore(size_t free_slots)
  : _free_slots(free_slots) {}

ss::future<> semaphore::start() { return _abort_sources.start(); }

ss::future<> semaphore::stop() {
    _is_closed = true;
    return _abort_sources.stop();
}

ss::future<> semaphore::acquire() {
    return ss::do_with(
      ss::lowres_clock::duration(10),
      [this](ss::lowres_clock::duration& wait_time) {
          return ss::do_until(
            [this] {
                if (_is_closed) {
                    throw ss::gate_closed_exception();
                }
                auto local_free_slots = _free_slots.load();
                if (local_free_slots > 0) {
                    return _free_slots.compare_exchange_strong(
                      local_free_slots, local_free_slots - 1);
                }
                return false;
            },
            [this, &wait_time] {
                return ss::sleep_abortable(
                         std::chrono::milliseconds(wait_time),
                         _abort_sources.local()._abort_source)
                  .then([&wait_time] {
                      if (wait_time < _max_wait_time_ms) {
                          wait_time *= 2;
                      }

                      return ss::now();
                  });
            });
      });
}

void semaphore::release() { _free_slots.fetch_add(1); }

} // namespace internal

// executor

executor::executor(size_t threads_num, uint8_t cpu_id)
  : _sem(threads_num)
  , _tasks(threads_num) {
    for (size_t i = 0; i < threads_num; i++) {
        _threads.emplace_back([this, cpu_id] {
            pin(cpu_id);
            loop();
        });
    }
}

executor::~executor() {
    for (auto& thread : _threads) {
        thread.join();
    }
}

ss::future<> executor::start() {
    return _sem.start().then([this] { return _gates.start(); });
}

ss::future<> executor::stop() {
    return _sem.stop().then([this] {
        return _gates.stop().then([this] {
            _is_stopped = true;
            _has_task_cv.notify_all();
        });
    });
}

bool executor::is_stopping() const {
    return _is_stopped.load(std::memory_order_relaxed);
}

void executor::pin(unsigned cpu_id) {
    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(cpu_id, &cs);
    [[maybe_unused]] auto r = pthread_setaffinity_np(
      pthread_self(), sizeof(cs), &cs);
}

void executor::loop() {
    for (;;) {
        internal::work_item* item = nullptr;

        {
            std::unique_lock lock{mutex};
            _has_task_cv.wait(lock, [this, &item] {
                return _tasks.pop(item) || is_stopping();
            });
        }

        if (is_stopping()) {
            break;
        } else {
            item->process();
        }
    }
}

} // namespace v8_engine
