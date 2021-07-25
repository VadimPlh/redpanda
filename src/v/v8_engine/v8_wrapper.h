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

#include "model/record.h"
#include "model/record_utils.h"
#include "seastarx.h"
#include "utils/utf8.h"
#include "v8.h"

#include <seastar/core/temporary_buffer.hh>

namespace v8_engine {

inline ss::temporary_buffer<char> get_buf(iobuf buf) {
    ss::temporary_buffer<char> temp_buf(buf.size_bytes());
    char* buf_ptr = temp_buf.get_write();
    for (auto& part : buf) {
        std::memcpy(buf_ptr, part.get_write(), part.size());
        buf_ptr += part.size();
    }

    return temp_buf;
}

struct record_wrapper {
    record_wrapper() = default;
    explicit record_wrapper(model::record& record) {
        size_bytes = record.size_bytes();
        offset = record.offset_delta();
        timestamp = record.timestamp_delta();
        key = get_buf(record.release_key());
        value = get_buf(record.release_value());
    }

    int size_bytes{};
    long offset{};
    long timestamp{};
    ss::temporary_buffer<char> key;
    ss::temporary_buffer<char> value;
};

struct record_batch_wrapper {
    explicit record_batch_wrapper(model::record_batch record_batch)
      : header(record_batch.header()) {
        record_batch.for_each_record(
          [this](model::record record) { old_records.emplace_back(record); });
    }

    model::record_batch convert_to_batch() {
        iobuf serialize_reords;
        for (auto& r : new_records) {
            iobuf key;
            auto key_size = r.key.size();
            key.append(std::move(r.key));
            iobuf value;
            auto value_size = r.value.size();
            value.append(std::move(r.value));

            static constexpr size_t zero_vint_size = vint::vint_size(0);
            auto size =  sizeof(model::record_attributes::type) // attributes
                   + vint::vint_size(r.timestamp)            // timestamp delta
                   + vint::vint_size(r.offset)        // offset_delta
                   + vint::vint_size(key.size_bytes())    // key size
                   + key.size_bytes()                     // key payload
                   + vint::vint_size(value.size_bytes())                  // value size
                   + value.size_bytes()
                   + zero_vint_size;

            model::record new_record(
              size,
              {},
              r.timestamp,
              r.offset,
              key_size == 0 ? -1 : key_size,
              std::move(key),
              value_size,
              std::move(value),
              {});

            model::append_record_to_buffer(serialize_reords, new_record);
        }


        header.size_bytes = serialize_reords.size_bytes()
                            + model::packed_record_batch_header_size;
        header.record_count = new_records.size();
        header.crc = model::crc_record_batch(header, serialize_reords);
         model::record_batch new_batch(header, std::move(serialize_reords));
        return new_batch;
    }

    model::record_batch_header header;

    std::vector<record_wrapper> old_records;
    std::vector<record_wrapper> new_records;
};

inline void
size(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Object> self = info.Holder();
    v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(
      self->GetInternalField(0));
    auto record_batch_ptr = static_cast<record_batch_wrapper*>(wrap->Value());

    info.GetReturnValue().Set(
      static_cast<uint32_t>(record_batch_ptr->old_records.size()));
}

inline void
get_record(uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Object> self = info.Holder();
    v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(
      self->GetInternalField(0));
    auto record_batch_ptr = static_cast<record_batch_wrapper*>(wrap->Value());

    if (index >= record_batch_ptr->old_records.size()) {
        throw "123";
    }

    auto map = v8::Map::New(info.GetIsolate());

    auto key_store = v8::ArrayBuffer::NewBackingStore(
      record_batch_ptr->old_records[index].key.get_write(),
      record_batch_ptr->old_records[index].key.size(),
      v8::BackingStore::EmptyDeleter,
      nullptr);
    auto data_array_buf = v8::ArrayBuffer::New(
      info.GetIsolate(), std::move(key_store));

    auto local_map = map->Set(
      info.GetIsolate()->GetCurrentContext(),
      v8::String::NewFromUtf8(info.GetIsolate(), "key").ToLocalChecked(),
      data_array_buf);

    auto value_store = v8::ArrayBuffer::NewBackingStore(
      record_batch_ptr->old_records[index].value.get_write(),
      record_batch_ptr->old_records[index].value.size(),
      v8::BackingStore::EmptyDeleter,
      nullptr);
    auto value_array_buf = v8::ArrayBuffer::New(
      info.GetIsolate(), std::move(value_store));

    local_map = local_map.ToLocalChecked()->Set(
      info.GetIsolate()->GetCurrentContext(),
      v8::String::NewFromUtf8(info.GetIsolate(), "value").ToLocalChecked(),
      value_array_buf);

    local_map = map->Set(
      info.GetIsolate()->GetCurrentContext(),
      v8::String::NewFromUtf8(info.GetIsolate(), "size_bytes").ToLocalChecked(),
      v8::Integer::New(
        info.GetIsolate(), record_batch_ptr->old_records[index].size_bytes));

    local_map = map->Set(
      info.GetIsolate()->GetCurrentContext(),
      v8::String::NewFromUtf8(info.GetIsolate(), "offset").ToLocalChecked(),
      v8::Integer::New(
        info.GetIsolate(), record_batch_ptr->old_records[index].offset));

    local_map = map->Set(
      info.GetIsolate()->GetCurrentContext(),
      v8::String::NewFromUtf8(info.GetIsolate(), "timestamp").ToLocalChecked(),
      v8::Integer::New(
        info.GetIsolate(), record_batch_ptr->old_records[index].timestamp));

    info.GetReturnValue().Set(local_map.ToLocalChecked());
}

inline void produce(
  v8::Local<v8::Name>,
  v8::Local<v8::Value> value,
  const v8::PropertyCallbackInfo<void>& info) {
    v8::Local<v8::Object> self = info.Holder();
    v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(
      self->GetInternalField(0));
    auto record_batch_ptr = static_cast<record_batch_wrapper*>(wrap->Value());

    record_wrapper produce_record;

    auto map = v8::Map::Cast(*value);

    auto key
      = map
          ->Get(
            info.GetIsolate()->GetCurrentContext(),
            v8::String::NewFromUtf8(info.GetIsolate(), "key").ToLocalChecked())
          .ToLocalChecked();
    auto array = v8::ArrayBuffer::Cast(*key);
    auto store = array->GetBackingStore();
    produce_record.key = ss::temporary_buffer<char>(
      reinterpret_cast<char*>(store->Data()), store->ByteLength());

    key = map
            ->Get(
              info.GetIsolate()->GetCurrentContext(),
              v8::String::NewFromUtf8(info.GetIsolate(), "value")
                .ToLocalChecked())
            .ToLocalChecked();
    array = v8::ArrayBuffer::Cast(*key);
    store = array->GetBackingStore();
    produce_record.value = ss::temporary_buffer<char>(
      reinterpret_cast<char*>(store->Data()), store->ByteLength());

    key = map
            ->Get(
              info.GetIsolate()->GetCurrentContext(),
              v8::String::NewFromUtf8(info.GetIsolate(), "offset")
                .ToLocalChecked())
            .ToLocalChecked();
    auto integer = v8::Integer::Cast(*key);
    produce_record.offset = integer->Value();

    key = map
            ->Get(
              info.GetIsolate()->GetCurrentContext(),
              v8::String::NewFromUtf8(info.GetIsolate(), "timestamp")
                .ToLocalChecked())
            .ToLocalChecked();
    integer = v8::Integer::Cast(*key);
    produce_record.timestamp = integer->Value();

    key = map
            ->Get(
              info.GetIsolate()->GetCurrentContext(),
              v8::String::NewFromUtf8(info.GetIsolate(), "size_bytes")
                .ToLocalChecked())
            .ToLocalChecked();
    integer = v8::Integer::Cast(*key);
    produce_record.size_bytes = integer->Value();

    record_batch_ptr->new_records.emplace_back(std::move(produce_record));
}

} // namespace v8_engine
