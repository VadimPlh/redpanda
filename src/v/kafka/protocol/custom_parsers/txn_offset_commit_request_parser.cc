
#include "kafka/protocol/custom_parsers/txn_offset_commit_request_parser.h"

#include "cluster/types.h"
#include "kafka/protocol/request_reader.h"
#include "kafka/protocol/response_writer.h"

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

namespace kafka {
void txn_offset_commit_request_data::encode(
  response_writer& writer, [[maybe_unused]] api_version version) {
    writer.write(transactional_id);
    writer.write(group_id);
    writer.write(producer_id);
    writer.write(producer_epoch);
    writer.write_array(
      topics,
      [version](txn_offset_commit_request_topic& v, response_writer& writer) {
          writer.write(v.name);
          writer.write_array(
            v.partitions,
            [version](
              txn_offset_commit_request_partition& v, response_writer& writer) {
                writer.write(v.partition_index);
                writer.write(v.committed_offset);
                if (version >= api_version(2)) {
                    writer.write(v.committed_leader_epoch);
                }
                writer.write(v.committed_metadata);
            });
      });
}
void txn_offset_commit_request_data::decode(
  request_reader& reader, [[maybe_unused]] api_version version) {
    if (version <= api_version(2)) {
        transactional_id = reader.read_string();
        group_id = kafka::group_id(reader.read_string());
        producer_id = kafka::producer_id(reader.read_int64());
        producer_epoch = reader.read_int16();
        topics = reader.read_array([version](request_reader& reader) {
            txn_offset_commit_request_topic v;
            v.name = model::topic(reader.read_string());
            v.partitions = reader.read_array([version](request_reader& reader) {
                txn_offset_commit_request_partition v;
                v.partition_index = model::partition_id(reader.read_int32());
                v.committed_offset = model::offset(reader.read_int64());
                if (version >= api_version(2)) {
                    v.committed_leader_epoch = kafka::leader_epoch(
                      reader.read_int32());
                }
                v.committed_metadata = reader.read_nullable_string();

                if (v.committed_metadata.has_value()) {
                    std::cout << v.committed_metadata.value();
                }
                return v;
            });
            return v;
        });
    } else {
        auto tag_size = reader.read_uvarint();
        vassert(tag_size == 0, "Do not expect tags");
        transactional_id = reader.read_compact_string().value();
        group_id = kafka::group_id(reader.read_compact_string().value());
        producer_id = kafka::producer_id(reader.read_int64());
        producer_epoch = reader.read_int16();
        generation_id = reader.read_int32();
        member_id = reader.read_compact_string().value();
        group_instance_id = reader.read_compact_string();
        topics
          = reader
              .read_compact_array([](request_reader& reader) {
                  txn_offset_commit_request_topic v;
                  v.name = model::topic(reader.read_compact_string().value());
                  v.partitions
                    = reader
                        .read_compact_array([](request_reader& reader) {
                            txn_offset_commit_request_partition v;
                            v.partition_index = model::partition_id(
                              reader.read_int32());
                            std::cout << v.partition_index << std::endl;
                            v.committed_offset = model::offset(
                              reader.read_int64());
                            std::cout << v.committed_offset << std::endl;
                            v.committed_leader_epoch = kafka::leader_epoch(
                              reader.read_int32());
                            std::cout << v.committed_leader_epoch << std::endl;
                            v.committed_metadata = reader.read_compact_string();
                            std::cout << v.committed_metadata.has_value() << std::endl;
                            reader.read_uvarint();
                            return v;
                        })
                        .value();
                  reader.read_uvarint();
                  return v;
              })
              .value();
    }
}

std::ostream&
operator<<(std::ostream& o, const txn_offset_commit_request_partition& v) {
    fmt::print(
      o,
      "{{partition_index={} committed_offset={} committed_leader_epoch={} "
      "committed_metadata={}}}",
      v.partition_index,
      v.committed_offset,
      v.committed_leader_epoch,
      v.committed_metadata);
    return o;
}

std::ostream&
operator<<(std::ostream& o, const txn_offset_commit_request_topic& v) {
    fmt::print(o, "{{name={} partitions={}}}", v.name, v.partitions);
    return o;
}

std::ostream&
operator<<(std::ostream& o, const txn_offset_commit_request_data& v) {
    fmt::print(
      o,
      "{{transactional_id={} group_id={} producer_id={} producer_epoch={} "
      "topics={}}}",
      v.transactional_id,
      v.group_id,
      v.producer_id,
      v.producer_epoch,
      v.topics);
    return o;
}

} // namespace kafka