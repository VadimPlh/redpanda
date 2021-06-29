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

#include "v8_engine/executor.h"

#include <libplatform/libplatform.h>

#include <v8.h>

namespace v8_engine {

class v8_platform_wrapper {
public:
    // Init single thread v8::Platform. If we use
    // NewDefaultPlatform platform it will create threadpool for background
    // tasks. Environment must be created before all v8 instance are starting!
    v8_platform_wrapper();

    v8_platform_wrapper(const v8_platform_wrapper& other) = delete;
    v8_platform_wrapper& operator=(const v8_platform_wrapper& other) = delete;
    v8_platform_wrapper(v8_platform_wrapper&& other) = delete;
    v8_platform_wrapper& operator=(v8_platform_wrapper&& other) = delete;

    // Stop v8 platform and release any resources used by v8.
    // Must be called after all v8 instances are finished!
    ~v8_platform_wrapper();

private:
    std::unique_ptr<v8::Platform> _platform;
};

// This class contains environment for v8.
// Can be used only one time per application.
class enviroment {
public:
    /// Init v8_platfrom and executor for v8.
    ///
    /// \param threads count in executor
    /// \param cpu_id for threads in executor
    enviroment(size_t threads_num, uint8_t cpu_id);

    enviroment(const enviroment& other) = delete;
    enviroment& operator=(const enviroment& other) = delete;
    enviroment(enviroment&& other) = delete;
    enviroment& operator=(enviroment&& other) = delete;

    // Need fot check that executor was stopped
    ~enviroment();

    // Functions for start and stop executor
    ss::future<> start();
    ss::future<> stop();

private:
    v8_platform_wrapper _platfrom;
    executor _executor;
};

} // namespace v8_engine
