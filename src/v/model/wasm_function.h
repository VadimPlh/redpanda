/*
 * Copyright 2021 Vectorized, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

#pragma once

#include "seastarx.h"
#include <seastar/core/sstring.hh>
#include <chrono>
#include <string>
#include "json/json.h"

namespace model {

class wasm_function {
public:
    wasm_function() = default;

    wasm_function(const wasm_function& other) = default;
    wasm_function& operator=(const wasm_function& other) = default;
    wasm_function(wasm_function&& other) = default;
    wasm_function& operator=(wasm_function&& other) = default;

    ~wasm_function() = default;

    wasm_function(std::string_view name, std::string_view path)
    : _name(name), _path(path) {}

    ss::sstring _name;
    ss::sstring _path;
    //std::chrono::milliseconds _timeout;
};

inline std::ostream& operator<<(std::ostream& os, wasm_function func) {
    os << "{name: " << func._name << " , path: " << func._path << "}";
    return os;
}

inline std::istream& operator>>(std::istream& i, wasm_function& func) {
    std::string s;
    std::getline(i, s);

    rapidjson::Document json_string;
    json_string.Parse(s);

    func._name = json_string["name"].GetString();
    func._path = json_string["path"].GetString();

    return i;
}

} // namespace model
