v_cc_library(
  NAME v8_engine
  SRCS
    environment.cc
    executor.cc
    script.cc
  DEPS
    Seastar::seastar
    v8_monolith
    absl::node_hash_map
    v::model
    v::utils)

add_subdirectory(tests)
