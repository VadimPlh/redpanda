
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
 * The txn_offset_commit_request_partition message.
 * 
 *        partition_index: The index of the partition within the topic.
 *                         Supported versions: [0, +inf)
 *       committed_offset: The message offset to be committed. Supported
 *                         versions: [0, +inf)
 * committed_leader_epoch: The leader epoch of the last consumed record.
 *                         Supported versions: [2, +inf)
 *     committed_metadata: Any associated metadata the client wants to keep.
 *                         Supported versions: [0, +inf)
 */
struct txn_offset_commit_request_partition {
    model::partition_id partition_index{};
    model::offset committed_offset{};
    kafka::leader_epoch committed_leader_epoch{-1};
    std::optional<ss::sstring> committed_metadata{};

    friend std::ostream& operator<<(std::ostream&, const txn_offset_commit_request_partition&);
};



/*
 * The txn_offset_commit_request_topic message.
 * 
 *       name: The topic name. Supported versions: [0, +inf)
 * partitions: The partitions inside the topic that we want to committ offsets
 *             for. Supported versions: [0, +inf)
 */
struct txn_offset_commit_request_topic {
    model::topic name{};
    std::vector<txn_offset_commit_request_partition> partitions{};

    friend std::ostream& operator<<(std::ostream&, const txn_offset_commit_request_topic&);
};




/*
 * The txn_offset_commit_request_data message.
 * 
 * transactional_id: The ID of the transaction. Supported versions: [0, +inf)
 *         group_id: The ID of the group. Supported versions: [0, +inf)
 *      producer_id: The current producer ID in use by the transactional ID.
 *                   Supported versions: [0, +inf)
 *   producer_epoch: The current epoch associated with the producer ID.
 *                   Supported versions: [0, +inf)
 *           topics: Each topic that we want to committ offsets for. Supported
 *                   versions: [0, +inf)
 */
struct txn_offset_commit_request_data {
    ss::sstring transactional_id{};
    kafka::group_id group_id{};
    kafka::producer_id producer_id{};
    int16_t producer_epoch{};
    int32_t generation_id{-1};
    ss::sstring member_id{};
    std::optional<ss::sstring> group_instance_id{};
    std::vector<txn_offset_commit_request_topic> topics{};

    void encode(response_writer&, api_version);
    void decode(request_reader&, api_version);

    friend std::ostream& operator<<(std::ostream&, const txn_offset_commit_request_data&);
};

}