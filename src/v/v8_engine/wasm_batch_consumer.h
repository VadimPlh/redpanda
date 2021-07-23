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

#include "cluster/topic_table.h"
#include "model/record_utils.h"
#include "v8_engine/executor.h"
#include "v8_engine/wasm_scripts_table.h"

#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>

namespace v8_engine {

template<typename InternalConsumer>
class wasm_batch_consumer {
public:
    explicit wasm_batch_consumer(
      wasm_scripts_table<executor_wrapper>& scripts_table,
      InternalConsumer internal_consumer, model::ntp ntp,
      model::wasm_function wasm_function)
      : _scripts_table(scripts_table)
      , _internal_consumer(std::move(internal_consumer)), _ntp(ntp),
      _wasm_function(wasm_function) {}

    ss::future<ss::stop_iteration> operator()(model::record_batch&& batch) {
        model::record_batch new_batch = co_await _scripts_table.run(_ntp, _wasm_function , std::move(batch));
        co_return co_await _internal_consumer(std::move(new_batch));
    }

    auto end_of_stream() { return _internal_consumer.end_of_stream(); }

private:
    wasm_scripts_table<executor_wrapper>& _scripts_table;
    InternalConsumer _internal_consumer;
    model::ntp _ntp;
    model::wasm_function _wasm_function;
};

} // namespace v8_engine
