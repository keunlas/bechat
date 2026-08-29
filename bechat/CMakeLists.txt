add_executable(bechat bechat.cpp)

target_sources(bechat PRIVATE
  core/acceptor.cpp
  core/session.cpp
  core/server_context.cpp
)

target_sources(bechat PRIVATE
  utils/io_threads.cpp
  utils/logger.cpp
)

target_sources(bechat PRIVATE
  tlv/tlv_message.cpp
  tlv/tlv_codec.cpp
)

target_sources(bechat PRIVATE
  proto/dispatcher.cpp
)

target_include_directories(bechat PRIVATE ${PROJECT_SOURCE_DIR})

