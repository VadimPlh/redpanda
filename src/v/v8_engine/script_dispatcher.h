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

#include "bytes/iobuf.h"
#include "model/metadata.h"
#include "v8_engine/script.h"

#include <seastar/core/coroutine.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/util/variant_utils.hh>

#include <absl/container/node_hash_map.h>

#include <exception>
#include <iostream>

namespace v8_engine {

struct script_compile_params {
    ss::sstring function_name;
    iobuf code;
    size_t max_heap_size_bytes{10000};
    size_t run_timeout_ms{200};
};

template<typename Executor>
class script_dispatcher {
public:
    explicit script_dispatcher(Executor& executor)
      : _executor(executor) {}

    void
    update(model::topic_namespace& topic, script_compile_params script_params) {
        _scripts.insert_or_assign(topic, std::move(script_params));
    }

    void remove(model::topic_namespace& topic) { _scripts.erase(topic); }

    // TODO: need to think about linearizability
    ss::future<ss::lw_shared_ptr<script>> get(model::topic_namespace& topic) {
        auto it = _scripts.find(topic);
        if (it == _scripts.end()) {
            co_return nullptr;
        }

        co_return co_await ss::visit(
          it->second,
          [this, topic](script_compile_params& tmp_params) mutable
          -> ss::future<ss::lw_shared_ptr<script>> {
              auto new_topic = std::move(topic);
              script_compile_params params = std::move(tmp_params);

              // Add nullptr to wait copilation finish in another future.
              _scripts.insert_or_assign(new_topic, nullptr);

              ss::lw_shared_ptr<script> new_runtime
                = ss::make_lw_shared<script>(
                  params.max_heap_size_bytes, params.run_timeout_ms);

              std::exception_ptr exception = nullptr;

              try {
                  std::cout << new_topic << std::endl;
                  co_await new_runtime->init(
                    std::move(params.function_name),
                    std::move(params.code),
                    _executor);
                  std::cout << new_topic << std::endl;
              } catch (const std::exception_ptr& e) {
                  exception = e;
              }

              auto it = _scripts.find(new_topic);
              if (it == _scripts.end()) {
                  // Somebody delete data-policy in compilation moment;
                  if (exception != nullptr) {
                      co_return nullptr;
                  } else {
                      std::rethrow_exception(exception);
                  }
              }

              if (exception != nullptr) {
                  _scripts.insert_or_assign(new_topic, new_runtime);
                  co_return new_runtime;
              } else {
                  // Need to return 
                  _scripts.insert_or_assign(new_topic, std::move(params));
                  std::rethrow_exception(exception);
              }
          },
          [](ss::lw_shared_ptr<script>& script_ptr)
            -> ss::future<ss::lw_shared_ptr<script>> { co_return script_ptr; });
    }

private:
    using value
      = std::variant<script_compile_params, ss::lw_shared_ptr<script>>;
    absl::node_hash_map<model::topic_namespace, value> _scripts;

    Executor& _executor;
};

} // namespace v8_engine