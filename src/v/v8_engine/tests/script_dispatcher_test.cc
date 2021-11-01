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
#include "utils/file_io.h"
#include "v8_engine/executor.h"
#include "v8_engine/script_dispatcher.h"

#include <seastar/core/reactor.hh>
#include <seastar/testing/thread_test_case.hh>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/test/tools/old/interface.hpp>

class executor_wrapper_for_test {
public:
    executor_wrapper_for_test()
      : _executor(ss::engine().alien(), 1, ss::smp::count) {}

    ~executor_wrapper_for_test() { _executor.stop().get(); }

    v8_engine::executor& get_executor() { return _executor; }

private:
    v8_engine::executor _executor;
};

v8_engine::enviroment env;

SEASTAR_THREAD_TEST_CASE(simple_update_test) {
    executor_wrapper_for_test executor_wrapper;

    model::ns ns("test");
    model::topic topic("1");
    model::topic_namespace topic_ns(ns, topic);

    v8_engine::script_dispatcher<v8_engine::executor> dispatcher(
      executor_wrapper.get_executor());

    auto js_code = read_fully("to_upper.js").get();

    v8_engine::script_compile_params to_upper{
      .function_name = "to_upper", .code = std::move(js_code)};

    dispatcher.update(topic_ns, std::move(to_upper));

    auto v8_runtime = dispatcher.get(topic_ns).get();
    BOOST_REQUIRE(v8_runtime.get() != nullptr);

    ss::sstring raw_data = "qwerty";
    ss::temporary_buffer<char> data(raw_data.data(), raw_data.size());
    v8_runtime->run(data.share(), executor_wrapper.get_executor()).get();
    boost::to_upper(raw_data);
    auto res = std::string(data.get_write(), data.size());
    BOOST_REQUIRE_EQUAL(raw_data, res);
}
