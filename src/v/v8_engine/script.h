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
#include "v8_engine/executor.h"

#include <seastar/core/future.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/core/timer.hh>

#include <chrono>
#include <v8.h>

namespace v8_engine {

class script_exception final : public std::exception {
public:
    explicit script_exception(ss::sstring msg) noexcept
      : _msg(std::move(msg)) {}

    const char* what() const noexcept final { return _msg.c_str(); }

private:
    ss::sstring _msg;
};

// This class implemented container for run js code inside it.
// It stores only one function from js code and can run it.
// v8::Isolate are used like container for js env.
// It can help isolate different functions from each other
// and set up constraints for them separately.
class script {
public:
    // Init new instance.
    // Create isolate and configure constraints for it.
    script(
      size_t max_heap_size_in_bytes,
      executor& executor_for_script,
      size_t _timeout_ms = 10);

    script(const script& other) = delete;
    script& operator=(const script& other) = delete;
    script(script&& other) = delete;
    script& operator=(script&& other) = delete;

    // Destroy instance. Be carefull!
    // First of all run Reset for all v8::Global fileds
    // Only after that run Dispose for isolate.
    ~script();

    /// Compile js code and get function for run in future.
    ///
    /// \param function name for runnig in future
    /// \param buffer with js code for compile
    ss::future<> init(ss::sstring name, ss::temporary_buffer<char> js_code);

    ss::future<> run(ss::temporary_buffer<char>& data);

private:
    // Must be running in executor, because it runs js code
    // in first time for init global vars and e.t.c.
    void compile_script(ss::temporary_buffer<char> js_code);

    // Init function from compiled js code.
    void set_function(std::string_view name);

    /// Run function from js code.
    /// Must be running in executor,
    /// because js function can have inf loop or smth like that.
    /// We need to controle execution time for js function

    /// \param buffer with data, wich js code can read and edit.
    void run_internal(ss::temporary_buffer<char>& data);

    // Throw c++ exception from v8::TryCatch
    void throw_exception_from_v8(std::string_view msg);
    void throw_exception_from_v8(
      const v8::TryCatch& try_catch, std::string_view msg);

    struct isolate_deleter {
    public:
        void operator()(v8::Isolate* isolate) const { isolate->Dispose(); }
    };

    // Stop v8 execution if it takes too much time
    void stop_execution();
    // Cancel stop execution for reusing isolate
    void continue_execution();

    std::unique_ptr<v8::Isolate, isolate_deleter> _isolate;

    v8::Global<v8::Context> _context;
    v8::Global<v8::Function> _function;

    executor& _executor; // TODO: maybe use unique_pointer without destructor
                         // for move v8_script in container with scripts

    // Watchdog for script timelimit
    seastar::timer<seastar::lowres_clock> _watchdog;
    // Mutex for lock watchdog for each sctipt run
    ss::semaphore _mutex{1};
    bool _is_cancel{false};

    // Script timeout
    std::chrono::milliseconds _timeout_ms;
    // Timeout for run script in first time for initialization
    static constexpr std::chrono::seconds _first_run_timeout_sec{10};
};

} // namespace v8_engine
