add_executable(bechat bechat.cpp)

target_sources(bechat PRIVATE
  core/acceptor.cpp
  core/session.cpp
)

target_sources(bechat PRIVATE
  utils/io_threads.cpp
  utils/logger.cpp
)

target_include_directories(bechat PRIVATE ${PROJECT_SOURCE_DIR})

