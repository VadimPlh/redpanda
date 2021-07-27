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

#include "model/record.h"
#include "model/record_utils.h"
#include "seastar/core/coroutine.hh"
#include "utils/file_io.h"

#include "v8_engine/v8_wrapper.h"

#include <seastar/core/future.hh>
#include <seastar/core/lowres_clock.hh>
#include <seastar/core/semaphore.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/util/later.hh>

#include <chrono>
#include <functional>
#include <string_view>
#include <utility>
#include <v8.h>

namespace v8_engine {

script::script(size_t , size_t timeout_ms)
  : _timeout_ms(timeout_ms) {
    v8::Isolate::CreateParams isolate_params;
    isolate_params.array_buffer_allocator_shared
      = std::shared_ptr<v8::ArrayBuffer::Allocator>(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    //isolate_params.constraints.ConfigureDefaultsFromHeapSize(
    //  0, max_heap_size_in_bytes);
    _isolate = std::unique_ptr<v8::Isolate, isolate_deleter>(
      v8::Isolate::New(isolate_params), isolate_deleter());
}

script::~script() {
    _function.Reset();
    _context.Reset();
}

static void LogCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);
    v8::Local<v8::Value> arg = args[0];
    v8::String::Utf8Value value(isolate, arg);
    std::cout << std::string(*value, value.length()) << std::endl;
}

bool script::compile_script(ss::temporary_buffer<char> js_code) {
    v8::Locker locker(_isolate.get());
    v8::Isolate::Scope isolate_scope(_isolate.get());
    v8::HandleScope handle_scope(_isolate.get());
    v8::TryCatch try_catch(_isolate.get());
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(
      _isolate.get());
    global->Set(
      _isolate.get(),
      "log",
      v8::FunctionTemplate::New(_isolate.get(), LogCallback));
    v8::Local<v8::Context> local_ctx = v8::Context::New(
      _isolate.get(), nullptr, global);
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
    return true;
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

/*
static void produce(
  v8::Local<v8::Name>,
  v8::Local<v8::Value> value,
  const v8::PropertyCallbackInfo<void>& info) {
    // v8::Local<v8::Object> self = info.Holder();
    // v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(
    //  self->GetInternalField(0));
    // auto record_batch_ptr =
    // static_cast<record_batch_wrapper*>(wrap->Value());

    auto map = v8::Map::Cast(*value);
    std::cout << "CAST MAP" << std::endl;

    auto key = map
                 ->Get(
                   info.GetIsolate()->GetCurrentContext(),
                   v8::String::NewFromUtf8(info.GetIsolate(), "value")
                     .ToLocalChecked())
                 .ToLocalChecked();
    std::cout << "GET KEY" << std::endl;
    auto array = v8::ArrayBuffer::Cast(*key);
    std::cout << "GET ARRAY" << std::endl;
    auto store = array->GetBackingStore();
    std::cout << "GET STORE" << std::endl;
    ss::temporary_buffer<char> key_buf(
      reinterpret_cast<char*>(store->Data()), store->ByteLength());
    std::cout << std::string(key_buf.get_write(), key_buf.size()) << std::endl;
}
*/

model::record_batch script::run_internal(model::record_batch data) {
    v8::Locker locker(_isolate.get());
    v8::Isolate::Scope isolate_scope(_isolate.get());
    v8::HandleScope handle_scope(_isolate.get());
    v8::TryCatch try_catch(_isolate.get());
    v8::Local<v8::Context> local_ctx = v8::Local<v8::Context>::New(
      _isolate.get(), _context);
    v8::Context::Scope context_scope(local_ctx);

    const int argc = 1;
    // auto store = v8::ArrayBuffer::NewBackingStore(
    //  data.get_write(), data.size(), v8::BackingStore::EmptyDeleter, nullptr);
    // auto data_array_buf = v8::ArrayBuffer::New(
    //  _isolate.get(), std::move(store));

    v8::Local<v8::ObjectTemplate> obj_templ = v8::ObjectTemplate::New(
      _isolate.get());
    obj_templ->SetInternalFieldCount(1);
    obj_templ->SetAccessor(
      v8::String::NewFromUtf8(_isolate.get(), "size").ToLocalChecked(), size);

    obj_templ->SetIndexedPropertyHandler(get_record);

    obj_templ->SetAccessor(
      v8::String::NewFromUtf8(_isolate.get(), "produce").ToLocalChecked(),
      nullptr, produce);

    record_batch_wrapper batch(std::move(data));

    v8::Local<v8::Object> obj
      = obj_templ->NewInstance(local_ctx).ToLocalChecked();
    obj->SetInternalField(0, v8::External::New(_isolate.get(), &batch));

    v8::Local<v8::Value> argv[argc] = {obj};
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

    return batch.convert_to_batch();
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
        _isolate->TerminateExecution();
    }
}

void script::cancel_terminate_execution_for_isolate() {
    if (_isolate->IsExecutionTerminating()) {
        _isolate->CancelTerminateExecution();
    }
}

} // namespace v8_engine
