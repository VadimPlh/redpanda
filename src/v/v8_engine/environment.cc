/*
 * Copyright 2021 Vectorized, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/vectorizedio/redpanda/blob/master/licenses/rcl.md
 */

#include "v8_engine/environment.h"

#include "vassert.h"

namespace v8_engine {

v8_platform_wrapper::v8_platform_wrapper() {
    v8::V8::SetFlagsFromString("--single-threaded");
    _platform = v8::platform::NewSingleThreadedDefaultPlatform();
    vassert(_platform.get() != nullptr, "Can not init platform for v8");
    v8::V8::InitializePlatform(_platform.get());
    v8::V8::Initialize();
}

v8_platform_wrapper::~v8_platform_wrapper() {
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
}

enviroment::enviroment(size_t threads_num, uint8_t cpu_id)
  : _executor(threads_num, cpu_id) {}

enviroment::~enviroment() {
    vassert(_executor.is_stopping(), "Executor was not stopped");
}

ss::future<> enviroment::start() { return _executor.start(); }

ss::future<> enviroment::stop() { return _executor.stop(); }

} // namespace v8_engine
