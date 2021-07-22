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

#include "model/fundamental.h"
#include "model/record_utils.h"
#include "model/wasm_function.h"
#include "seastarx.h"
#include "utils/file_io.h"
#include "v8_engine/script.h"

#include <seastar/core/coroutine.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>

#include <absl/container/node_hash_map.h>
#include <tuple>

namespace v8_engine {

template<typename Executor>
class wasm_scripts_table {
public:
    explicit wasm_scripts_table(Executor& executor)
    : _executor(executor) {}

    ss::future<model::record_batch> run(model::ntp ntp, model::wasm_function topic_wasm_prop, model::record_batch&& batch) {
        if (topic_wasm_prop._path.empty()) {
            throw "GGGGGG";
        }

        co_await init_script(ntp, topic_wasm_prop);
        co_return co_await run_script(ntp, std::forward<model::record_batch>(batch));
    }

private:
    ss::future<>
    init_script(model::ntp ntp, model::wasm_function topic_wasm_prop) {
        std::cout << 1 << std::endl;
        auto it = _scripts.find(ntp);
        if (it == _scripts.end()) {
            std::cout << 2 << std::endl;
            it = _scripts.emplace(ntp, script(100, 100)).first;
            std::cout << 3 << std::endl;

            ss::temporary_buffer<char> wasm_code = co_await read_fully_tmpbuf(
              std::filesystem::path(topic_wasm_prop._path));
              std::cout << 4 << std::endl;
            co_await it->second.init(
              topic_wasm_prop._name, std::move(wasm_code), _executor);
        }
        co_return;
    }

    ss::future<model::record_batch> run_script(model::ntp ntp, model::record_batch&& batch) {
        auto it = _scripts.find(ntp);
        if (it == _scripts.end()) {
            throw "123";
        }
        script* script_ptr = &it->second;
        co_return co_await script_ptr->run(std::forward<model::record_batch>(batch), _executor);
    }

    absl::node_hash_map<model::ntp, script> _scripts;
    Executor& _executor;
};

} // namespace v8_engine
