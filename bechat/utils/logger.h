/**
 * @file logger.h
 * @author Keunlas
 * @brief 对 spdlog 的封装
 * @date 2026-08-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_UTILS_LOGGER_H_
#define BECHAT_UTILS_LOGGER_H_

#include <format>
#include <memory>
#include <utility>

#define SPDLOG_USE_STD_FORMAT
#include <spdlog/spdlog.h>

class Logger {
 public:
  static std::shared_ptr<spdlog::logger> AsyncConsoleLogger();
};

#define CURRENT_LOGGER Logger::AsyncConsoleLogger()

#define LOG_IF(level, ...)                     \
  do {                                         \
    if (CURRENT_LOGGER->should_log(level))     \
      CURRENT_LOGGER->log(level, __VA_ARGS__); \
  } while (0)

#define TRACE(...) LOG_IF(spdlog::level::trace, __VA_ARGS__)
#define DEBUG(...) LOG_IF(spdlog::level::debug, __VA_ARGS__)
#define INFO(...) LOG_IF(spdlog::level::info, __VA_ARGS__)
#define WARN(...) LOG_IF(spdlog::level::warn, __VA_ARGS__)
#define ERROR(...) LOG_IF(spdlog::level::err, __VA_ARGS__)
#define CRITICAL(...) LOG_IF(spdlog::level::critical, __VA_ARGS__)

#endif  // !BECHAT_UTILS_LOGGER_H_
