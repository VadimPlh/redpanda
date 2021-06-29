/*
 * Copyright 2021 Vectorized, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/vectorizedio/redpanda/blob/master/licenses/rcl.md
 */

#include "v8_engine/script.h"

#include "utils/file_io.h"

#include <seastar/core/future.hh>
#include <seastar/core/lowres_clock.hh>
#include <seastar/core/semaphore.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/util/later.hh>

#include <chrono>
#include <string_view>
#include <utility>

namespace v8_engine {

script::script(
  size_t max_heap_size_in_bytes,
  executor& executor_for_script,
  size_t timeout_ms)
  : _executor(executor_for_script)
  , _timeout_ms(timeout_ms) {
    v8::Isolate::CreateParams isolate_params;
    isolate_params.array_buffer_allocator_shared
      = std::shared_ptr<v8::ArrayBuffer::Allocator>(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    isolate_params.constraints.ConfigureDefaultsFromHeapSize(
      0, max_heap_size_in_bytes);
    _isolate = std::unique_ptr<v8::Isolate, isolate_deleter>(
      v8::Isolate::New(isolate_params), isolate_deleter());

    _watchdog.set_callback([this] { stop_execution(); });
}

script::~script() {
    _function.Reset();
    _context.Reset();
}

ss::future<>
script::init(ss::sstring name, ss::temporary_buffer<char> js_code) {
    return ss::with_semaphore(
      _mutex,
      1,
      [this, js_code = std::move(js_code), name = std::move(name)]() mutable {
          return _executor
            .submit(
              [this] {
                  _is_cancel = false;
                  _watchdog.rearm(seastar::lowres_clock::time_point(
                    seastar::lowres_clock::now() + _first_run_timeout_sec));
              },
              [this, js_code = std::move(js_code)]() mutable {
                  compile_script(std::move(js_code));
              })
            .handle_exception_type([this](const ss::sleep_aborted&) {
                throw_exception_from_v8("Executor is closed");
            })
            .handle_exception_type([this](const ss::gate_closed_exception&) {
                throw_exception_from_v8("Executor is closed");
            })
            .finally([this] {
                if (!_is_cancel) {
                    _watchdog.cancel();
                } else {
                    continue_execution();
                }
            })
            .then([this, name] { set_function(name); });
      });
}

void script::compile_script(ss::temporary_buffer<char> js_code) {
    v8::Locker locker(_isolate.get());
    v8::Isolate::Scope isolate_scope(_isolate.get());
    v8::HandleScope handle_scope(_isolate.get());
    v8::TryCatch try_catch(_isolate.get());
    v8::Local<v8::Context> local_ctx = v8::Context::New(_isolate.get());
    v8::Context::Scope context_scope(local_ctx);

    v8::Local<v8::String> script_code = v8::String::NewFromUtf8(
                                          _isolate.get(),
                                          js_code.begin(),
                                          v8::NewStringType::kNormal,
                                          js_code.size())
                                          .ToLocalChecked();
    v8::Local<v8::Script> compiled_script;
    if (!v8::Script::Compile(local_ctx, script_code)
           .ToLocal(&compiled_script)) {
        throw_exception_from_v8(try_catch, "Can not compile script");
    }

    // Run js code in first time and init some data from them.
    // Need to be running in executor.
    v8::Local<v8::Value> result;
    if (!compiled_script->Run(local_ctx).ToLocal(&result)) {
        if (try_catch.Exception()->IsNull() && try_catch.Message().IsEmpty()) {
            throw_exception_from_v8(
              "Can not run script in first time(timeout)");
        } else {
            throw_exception_from_v8(
              try_catch, "Can not run script in first time");
        }
    }

    _context.Reset(_isolate.get(), local_ctx);
}

void script::set_function(std::string_view name) {
    v8::Locker locker(_isolate.get());
    v8::Isolate::Scope isolate_scope(_isolate.get());
    v8::HandleScope handle_scope(_isolate.get());
    v8::Local<v8::Context> local_ctx = v8::Local<v8::Context>::New(
      _isolate.get(), _context);
    v8::Context::Scope context_scope(local_ctx);
    v8::Local<v8::String> function_name
      = v8::String::NewFromUtf8(_isolate.get(), name.data()).ToLocalChecked();
    v8::Local<v8::Value> function_val;
    if (
      !local_ctx->Global()->Get(local_ctx, function_name).ToLocal(&function_val)
      || !function_val->IsFunction()) {
        throw script_exception(
          fmt::format("Can not get function({}) from script", name));
    }

    _function.Reset(_isolate.get(), function_val.As<v8::Function>());
}

ss::future<> script::run(ss::temporary_buffer<char>& data) {
    return ss::with_semaphore(_mutex, 1, [this, &data] {
        return _executor
          .submit(
            [this] {
                _is_cancel = false;
                _watchdog.rearm(seastar::lowres_clock::time_point(
                  seastar::lowres_clock::now() + _timeout_ms));
            },
            [this, &data] { run_internal(data); })
          .handle_exception_type([this](const ss::sleep_aborted&) {
              throw_exception_from_v8("Executor is closed");
          })
          .handle_exception_type([this](const ss::gate_closed_exception&) {
              throw_exception_from_v8("Executor is closed");
          })
          .finally([this] {
              if (!_is_cancel) {
                  _watchdog.cancel();
              } else {
                  continue_execution();
              }
          });
    });
}

void script::run_internal(ss::temporary_buffer<char>& data) {
    v8::Locker locker(_isolate.get());
    v8::Isolate::Scope isolate_scope(_isolate.get());
    v8::HandleScope handle_scope(_isolate.get());
    v8::TryCatch try_catch(_isolate.get());
    v8::Local<v8::Context> local_ctx = v8::Local<v8::Context>::New(
      _isolate.get(), _context);
    v8::Context::Scope context_scope(local_ctx);

    const int argc = 1;
    auto store = v8::ArrayBuffer::NewBackingStore(
      data.get_write(), data.size(), v8::BackingStore::EmptyDeleter, nullptr);
    auto data_array_buf = v8::ArrayBuffer::New(
      _isolate.get(), std::move(store));
    v8::Local<v8::Value> argv[argc] = {data_array_buf};
    v8::Local<v8::Value> result;

    v8::Local<v8::Function> local_function = v8::Local<v8::Function>::New(
      _isolate.get(), _function);
    if (!local_function->Call(local_ctx, local_ctx->Global(), argc, argv)
           .ToLocal(&result)) {
        // StopExecution generate exception without message
        if (try_catch.Exception()->IsNull() && try_catch.Message().IsEmpty()) {
            throw_exception_from_v8("Sript timeout");
        } else {
            throw_exception_from_v8(try_catch, "Can not run function");
        }
    }
}

void script::throw_exception_from_v8(std::string_view msg) {
    throw script_exception(fmt::format("{}", msg));
}

void script::throw_exception_from_v8(
  const v8::TryCatch& try_catch, std::string_view msg) {
    v8::String::Utf8Value error(_isolate.get(), try_catch.Exception());
    throw script_exception(
      fmt::format("{}:{}", msg, std::string(*error, error.length())));
}

void script::stop_execution() {
    if (!_isolate->IsExecutionTerminating()) {
        _is_cancel = true;
        _isolate->TerminateExecution();
    }
}

void script::continue_execution() {
    if (_isolate->IsExecutionTerminating()) {
        _isolate->CancelTerminateExecution();
    }
}

} // namespace v8_engine
