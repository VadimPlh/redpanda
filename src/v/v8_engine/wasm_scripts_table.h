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
        co_return co_await run_script(ntp, std::move(batch));
    }

private:
    ss::future<>
    init_script(model::ntp ntp, model::wasm_function topic_wasm_prop) {
        auto it = _scripts.find(ntp);

        if (it == _scripts.end() || it->second.first != topic_wasm_prop) {
            std::pair<model::wasm_function, script> new_foo = {topic_wasm_prop, script(100, 100000)};
            if (it != _scripts.end()) {
                _scripts.erase(it);
            }

            it = _scripts.emplace(ntp, std::move(new_foo)).first;

            ss::temporary_buffer<char> wasm_code = co_await read_fully_tmpbuf(
              std::filesystem::path(topic_wasm_prop._path));
            co_await it->second.second.init(
              topic_wasm_prop._name, std::move(wasm_code), _executor);
        }
        co_return;
    }

    ss::future<model::record_batch> run_script(model::ntp ntp, model::record_batch&& batch) {
        auto it = _scripts.find(ntp);
        if (it == _scripts.end()) {
            throw "123";
        }
        co_return co_await it->second.second.run(std::move(batch), _executor);
    }

    absl::node_hash_map<model::ntp, std::pair<model::wasm_function, script>> _scripts;
    Executor& _executor;
};

} // namespace v8_engine
