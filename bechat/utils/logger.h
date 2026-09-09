#ifndef BECHAT_UTILS_LOGGER_H_
#define BECHAT_UTILS_LOGGER_H_

#include <spdlog/spdlog.h>

#include <format>
#include <memory>
#include <utility>

class Logger {
 public:
  static std::shared_ptr<spdlog::logger> AsyncConsoleLogger();
};

#define active_logger Logger::AsyncConsoleLogger()

/**
 * @brief Macro for using logger conveniently
 *
 */

#define LOG_IF(level, ...)                    \
  do {                                        \
    if (active_logger->should_log(level)) {   \
      active_logger->log(level, __VA_ARGS__); \
    }                                         \
  } while (0)

#define TRACE(...) LOG_IF(spdlog::level::trace, __VA_ARGS__)

#define DEBUG(...) LOG_IF(spdlog::level::debug, __VA_ARGS__)

#define INFO(...) LOG_IF(spdlog::level::info, __VA_ARGS__)

#define WARN(...) LOG_IF(spdlog::level::warn, __VA_ARGS__)

#define ERROR(...) LOG_IF(spdlog::level::err, __VA_ARGS__)

#define CRITICAL(...) LOG_IF(spdlog::level::critical, __VA_ARGS__)

#endif  // !BECHAT_UTILS_LOGGER_H_
