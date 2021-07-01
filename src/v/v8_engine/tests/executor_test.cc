/*
 * Copyright 2021 Vectorized, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/vectorizedio/redpanda/blob/master/licenses/rcl.md
 */

#include "seastarx.h"
#include "v8_engine/executor.h"

#include <seastar/core/sleep.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/when_all.hh>
#include <seastar/testing/thread_test_case.hh>

#include <algorithm>

SEASTAR_THREAD_TEST_CASE(task_queue_full_test) {
    struct task_for_test {
        void operator()() { sleep(1); }

        void cancel() noexcept {};

        void on_timeout() noexcept {}
    };

    v8_engine::executor test_executor(1, ss::smp::count);

    std::vector<ss::future<>> futures;
    futures.reserve(3);

    for (int i = 0; i < 3; ++i) {
        futures.emplace_back(test_executor.submit(
          task_for_test(), std::chrono::milliseconds(5000)));
    }

    ss::when_all_succeed(futures.begin(), futures.end()).get();
    test_executor.stop().get();
}

SEASTAR_THREAD_TEST_CASE(simple_stop_executor_test) {
    struct task_for_test {
        void operator()() {}

        void cancel() noexcept {}

        void on_timeout() noexcept {}
    };

    v8_engine::executor test_executor(1, ss::smp::count);
    test_executor.stop().get();
    auto fut = test_executor.submit(
      task_for_test(), std::chrono::milliseconds(5000));

    BOOST_REQUIRE_EXCEPTION(
      fut.get(),
      ss::gate_closed_exception,
      [](const ss::gate_closed_exception& e) {
          return "gate closed" == std::string(e.what());
      });
}

SEASTAR_THREAD_TEST_CASE(task_with_exception_test) {
    class test_exception final : public std::exception {
    public:
        explicit test_exception(ss::sstring msg) noexcept
        : _msg(std::move(msg)) {}

        const char* what() const noexcept final { return _msg.c_str(); }

    private:
        ss::sstring _msg;
    };

    struct task_for_test {
        void operator()() { throw test_exception("Test exception"); }

        void cancel() noexcept {}

        void on_timeout() noexcept {}
    };

    v8_engine::executor test_executor(1, ss::smp::count);

    auto fut = test_executor.submit(
      task_for_test(), std::chrono::milliseconds(20));

    BOOST_REQUIRE_EXCEPTION(
      fut.get(),
      test_exception,
      [](const test_exception& e) {
          return "Test exception" == std::string(e.what());
      });

    test_executor.stop().get();
}

SEASTAR_THREAD_TEST_CASE(stop_executor_with_finish_item_test) {
    struct task_for_test {
        explicit task_for_test(char& is_finish)
          : _is_finish(is_finish) {}

        void operator()() {
            sleep(1);
            _is_finish = 1;
        }

        void cancel() noexcept {}

        void on_timeout() noexcept {}

        char& _is_finish;
    };

    v8_engine::executor test_executor(1, ss::smp::count);

    const size_t futures_count = 5;

    std::vector<char> finish_flags(futures_count, 0);
    std::vector<ss::future<>> futures;
    futures.reserve(futures_count);

    for (int i = 0; i < futures_count; ++i) {
        futures.emplace_back(test_executor.submit(
          task_for_test(finish_flags[i]), std::chrono::milliseconds(5000)));
    }

    test_executor.stop().get();
    auto res = ss::when_all(futures.begin(), futures.end()).get();

    for (auto i = 0; i < futures_count; ++i) {
        if (finish_flags[i] != 1) {
            BOOST_REQUIRE_EXCEPTION(
              res[i].get(),
              ss::gate_closed_exception,
              [](const ss::gate_closed_exception& e) {
                  return "gate closed" == std::string(e.what());
              });
        }
    }
}