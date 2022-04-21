
#include "kafka/protocol/custom_parsers/txn_offset_commit_response_parser.h"

#include "cluster/types.h"
#include "kafka/protocol/request_reader.h"
#include "kafka/protocol/response_writer.h"

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

namespace kafka {
void txn_offset_commit_response_data::encode(
  response_writer& writer, [[maybe_unused]] api_version version) {
    std::cout << version << std::endl;
    if (version <= api_version(2)) {
        writer.write(throttle_time_ms);
        writer.write_array(
          topics,
          [](txn_offset_commit_response_topic& v, response_writer& writer) {
              writer.write(v.name);
              writer.write_array(
                v.partitions,
                [](
                  txn_offset_commit_response_partition& v,
                  response_writer& writer) {
                    writer.write(v.partition_index);
                    writer.write(v.error_code);
                });
          });
    } else {
        writer.write_uvint(0);
        writer.write(throttle_time_ms);
        std::optional<std::vector<txn_offset_commit_response_topic>> d = topics;
        writer.write_compact_array(
          d, [](txn_offset_commit_response_topic& v, response_writer& writer) {
              std::optional<ss::sstring> n = v.name;
              writer.write_compact_string(n);
              std::optional<std::vector<txn_offset_commit_response_partition>> p
                = v.partitions;
              writer.write_compact_array(
                p,
                [](
                  txn_offset_commit_response_partition& v,
                  response_writer& writer) {
                    writer.write(v.partition_index);
                    writer.write(v.error_code);
                    writer.write_uvint(0);
                });
                writer.write_uvint(0);
          });
    }
}
void txn_offset_commit_response_data::decode(
  iobuf buf, [[maybe_unused]] api_version version) {
    request_reader reader(std::move(buf));
    throttle_time_ms = std::chrono::milliseconds(reader.read_int32());
    topics = reader.read_array([](request_reader& reader) {
        txn_offset_commit_response_topic v;
        v.name = model::topic(reader.read_string());
        v.partitions = reader.read_array([](request_reader& reader) {
            txn_offset_commit_response_partition v;
            v.partition_index = reader.read_int32();
            v.error_code = kafka::error_code(reader.read_int16());
            return v;
        });
        return v;
    });
}

std::ostream&
operator<<(std::ostream& o, const txn_offset_commit_response_partition& v) {
    fmt::print(
      o,
      "{{partition_index={} error_code={}}}",
      v.partition_index,
      v.error_code);
    return o;
}

std::ostream&
operator<<(std::ostream& o, const txn_offset_commit_response_topic& v) {
    fmt::print(o, "{{name={} partitions={}}}", v.name, v.partitions);
    return o;
}

std::ostream&
operator<<(std::ostream& o, const txn_offset_commit_response_data& v) {
    fmt::print(
      o, "{{throttle_time_ms={} topics={}}}", v.throttle_time_ms, v.topics);
    return o;
}

} // namespace kafka