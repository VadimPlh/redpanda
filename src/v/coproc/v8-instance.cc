#include "coproc/v8-instance.h"

namespace coproc {

v8_instance::v8_instance(v8::Isolate::CreateParams create_params_)
  : create_params(std::move(create_params_)),
    isolate(v8::Isolate::New(create_params)) {
        watchdog.set_callback([this]{
            stop_execution_loop();
            is_canceled = true;
        });
    }

v8_instance::~v8_instance() {
    context.Reset();
    function.Reset();
    isolate->Dispose();
}

seastar::future<bool> v8_instance::init_instance(const std::string script_path) {
    return compile_script(script_path)
    .then([this](bool result){
        std::cout << result << std::endl;
        return create_script();
    });
}

seastar::future<bool> v8_instance::run_instance(ThreadPool& thread_pool, int timeout, std::span<char> data) {
    return seastar::with_semaphore(mtx, 1, [this, &thread_pool, timeout, data](){
        is_canceled = false;
        watchdog.rearm(seastar::lowres_clock::time_point(seastar::lowres_clock::now() + std::chrono::seconds(timeout)));
        return thread_pool.submit([this, data](){
            run_instance_internal(data);
        })
        .then([this] {
            if (!is_canceled) {
                watchdog.cancel();
            }
            return seastar::make_ready_future<bool>(is_canceled);
        });
    });
}

void v8_instance::stop_execution_loop() {
    if (!isolate->IsExecutionTerminating()) {
        isolate->TerminateExecution();
    } else {
        std::cout << "We have beed alreade stoped this thread" << std::endl;
    }
}

void v8_instance::continue_execution() {
    if (isolate->IsExecutionTerminating()) {
        isolate->CancelTerminateExecution();
    }
}

seastar::future<seastar::temporary_buffer<char>> v8_instance::read_file(const std::string script_path) {
    return seastar::with_file(seastar::open_file_dma(script_path, seastar::open_flags::ro), [](seastar::file f){
        return f.size()
        .then([f](size_t size) mutable {
            return f.dma_read<char>(0, size);
        });
    });
}

seastar::future<bool> v8_instance::compile_script(const std::string script_path) {
    return read_file(script_path)
    .then([this](const seastar::temporary_buffer<char> script) mutable {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Context> local_ctx = v8::Context::New(isolate);
        v8::Context::Scope context_scope(local_ctx);

        v8::Local<v8::String> script_code = v8::String::NewFromUtf8(isolate, script.begin(), v8::NewStringType::kNormal, script.size()).ToLocalChecked();
        v8::Local<v8::Script> compiled_script;
        if (!v8::Script::Compile(local_ctx, script_code).ToLocal(&compiled_script)) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cout << "Can not compile script: " << std::string(*error, error.length()) << std::endl;
            return seastar::make_ready_future<bool>(false);
        }

        v8::Local<v8::Value> result;
        if (!compiled_script->Run(local_ctx).ToLocal(&result)) {
            std::cout << "Run script error\n" << std::endl;
            return seastar::make_ready_future<bool>(false);
        }

        context.Reset(isolate, local_ctx);
        return seastar::make_ready_future<bool>(true);
    });
}

seastar::future<bool> v8_instance::create_script() {
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> local_ctx = v8::Local<v8::Context>::New(isolate, context);
    v8::Context::Scope context_scope(local_ctx);

    v8::Local<v8::String> function_name = v8::String::NewFromUtf8Literal(isolate, "user_script");
    v8::Local<v8::Value> function_val;
    if (!local_ctx->Global()->Get(local_ctx, function_name).ToLocal(&function_val) || !function_val->IsFunction()) {
        std::cout << "Can not create process for function" << std::endl;
        return seastar::make_ready_future<bool>(false) ;
    }

    function.Reset(isolate, function_val.As<v8::Function>());
    return seastar::make_ready_future<bool>(true);
}

bool v8_instance::run_instance_internal(std::span<char> data) {
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::TryCatch try_catch(isolate);
    v8::Local<v8::Context> local_ctx = v8::Local<v8::Context>::New(isolate, context);
    v8::Context::Scope context_scope(local_ctx);

    const int argc = 1;
    auto store = v8::ArrayBuffer::NewBackingStore(data.data(), data.size(), v8::BackingStore::EmptyDeleter, nullptr);
    auto array = v8::ArrayBuffer::New(isolate, std::move(store));
    v8::Local<v8::Value> argv[argc] = { array };
    v8::Local<v8::Value> result;

    v8::Local<v8::Function> local_function = v8::Local<v8::Function>::New(isolate, function);
    if (!local_function->Call(local_ctx, local_ctx->Global(), argc, argv).ToLocal(&result)) {
        v8::String::Utf8Value error(isolate, try_catch.Exception());
        std::cout << "Can not run script: " << std::string(*error, error.length()) << std::endl;
        return false;
    }

    return true;
}

}