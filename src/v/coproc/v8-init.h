#pragma once

#include "v8-includes.h"

namespace coproc {

std::unique_ptr<v8::Platform> init_v8_platform();

void stop_v8_platform();

} // namespace coproc