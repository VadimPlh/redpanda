#pragma once
#include "kafka/types.h"
#include "model/fundamental.h"
#include "model/metadata.h"
#include "kafka/protocol/batch_reader.h"
#include "kafka/protocol/errors.h"
#include "model/timestamp.h"
#include "seastarx.h"

#include <seastar/core/sstring.hh>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>



namespace kafka {

class request_reader;
class response_writer;
class response;



/*
 * The txn_offset_commit_response_partition message.
 * 
 * partition_index: The partitition index. Supported versions: [0, +inf)
 *      error_code: The error code, or 0 if there was no error. Supported
 *                  versions: [0, +inf)
 */
struct txn_offset_commit_response_partition {
    int32_t partition_index{};
    kafka::error_code error_code{};

    friend std::ostream& operator<<(std::ostream&, const txn_offset_commit_response_partition&);
};



/*
 * The txn_offset_commit_response_topic message.
 * 
 *       name: The topic name. Supported versions: [0, +inf)
 * partitions: The responses for each partition in the topic. Supported
 *             versions: [0, +inf)
 */
struct txn_offset_commit_response_topic {
    model::topic name{};
    std::vector<txn_offset_commit_response_partition> partitions{};

    friend std::ostream& operator<<(std::ostream&, const txn_offset_commit_response_topic&);
};




/*
 * The txn_offset_commit_response_data message.
 * 
 * throttle_time_ms: The duration in milliseconds for which the request was
 *                   throttled due to a quota violation, or zero if the request
 *                   did not violate any quota. Supported versions: [0, +inf)
 *           topics: The responses for each topic. Supported versions: [0, +inf)
 */
struct txn_offset_commit_response_data {
    std::chrono::milliseconds throttle_time_ms{0};
    std::vector<txn_offset_commit_response_topic> topics{};

    void encode(response_writer&, api_version);
    void decode(iobuf, api_version);

    friend std::ostream& operator<<(std::ostream&, const txn_offset_commit_response_data&);
};

}