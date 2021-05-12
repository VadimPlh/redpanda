#pragma once

#include <span>

#include "coproc/v8-includes.h"
#include "coproc/native-thread-pool.h"

#include <seastar/core/do_with.hh>
#include <seastar/core/file-types.hh>
#include <seastar/core/file.hh>
#include <seastar/core/lowres_clock.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/core/future.hh>
#include <seastar/core/seastar.hh>

namespace coproc {

class v8_instance {
public:
    v8_instance(v8::Isolate::CreateParams);

    ~v8_instance();

    seastar::future<bool> init_instance(const std::string);

    seastar::future<bool> run_instance(ThreadPool&, int, std::span<char>);

    void stop_execution_loop();

    void continue_execution();

private:
    seastar::future<seastar::temporary_buffer<char>> read_file(const std::string);

    seastar::future<bool> compile_script(const std::string);

    seastar::future<bool> create_script();

    bool run_instance_internal(std::span<char>);

    v8::Isolate::CreateParams create_params;
    v8::Isolate* isolate{};

    v8::Global<v8::Context> context;
    v8::Global<v8::Function> function;

    bool is_canceled;
    seastar::timer<seastar::lowres_clock> watchdog;

    seastar::semaphore mtx{1};
};

}