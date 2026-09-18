add_executable(bechat bechat.cpp)
target_compile_definitions(bechat PRIVATE SPDLOG_USE_STD_FORMAT)
target_include_directories(bechat PRIVATE ${PROJECT_SOURCE_DIR})
target_include_directories(bechat PRIVATE ${PROJECT_SOURCE_DIR}/deps/include)

target_sources(bechat PRIVATE
  core/io_contexts.cpp
  core/server.cpp
  core/ssl_server.cpp
  core/server_context.cpp
)

target_sources(bechat PRIVATE
  utils/logger.cpp
  utils/config.cpp
  utils/global.cpp
)

target_sources(bechat PRIVATE
  service/user_registry.cpp
)

target_sources(bechat PRIVATE
  # tlv/tlv_message.cpp
  # tlv/tlv_codec.cpp
)

target_sources(bechat PRIVATE
  # proto/request_message.cpp
)

# Link OpenSSL
target_link_libraries(bechat PRIVATE ssl crypto)

# Link libsodium
target_link_libraries(bechat PRIVATE sodium)
