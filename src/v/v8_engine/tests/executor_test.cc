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
#include <seastar/testing/thread_test_case.hh>

SEASTAR_THREAD_TEST_CASE(stop_executor_in_first_function_test) {
    v8_engine::executor test_executor(1, 0);
    test_executor.start().get();

    auto end_flag = false;
    auto fut = test_executor.submit(
      [] { sleep(2); }, [&end_flag] { end_flag = true; });
    test_executor.stop().get();
    fut.get();
    BOOST_REQUIRE_EQUAL(end_flag, true);
}

SEASTAR_THREAD_TEST_CASE(stop_executor_in_second_function_test) {
    v8_engine::executor test_executor(1, 0);
    test_executor.start().get();

    auto end_flag = false;
    auto fut = test_executor.submit(
      [] {},
      [&end_flag] {
          sleep(2);
          end_flag = true;
      });
    test_executor.stop().get();
    fut.get();
    BOOST_REQUIRE_EQUAL(end_flag, true);
}

SEASTAR_THREAD_TEST_CASE(try_to_run_task_after_stop_test) {
    v8_engine::executor test_executor(1, 0);
    test_executor.start().get();

    auto first_fut = test_executor.submit([] {}, [] { sleep(2); });

    auto end_flag = false;
    auto second_fut = test_executor.submit(
      [] {}, [&end_flag] { end_flag = true; });

    test_executor.stop().get();
    first_fut.get();

    BOOST_REQUIRE_EXCEPTION(
      second_fut.get(), ss::sleep_aborted, [](const ss::sleep_aborted& e) {
          return "Sleep is aborted" == std::string(e.what());
      });

    BOOST_REQUIRE_EQUAL(end_flag, false);
}
