#ifndef BECHAT_LOGGER_H_
#define BECHAT_LOGGER_H_

#define SPDLOG_USE_STD_FORMAT
#include <spdlog/spdlog.h>

#include <format>
#include <memory>
#include <utility>

class Logger {
 public:
  static std::shared_ptr<spdlog::logger> AsyncConsoleLogger();
};

#define CURRENT_LOGGER Logger::AsyncConsoleLogger()

class Log {
 public:
  template <typename... Args>
  inline static void Trace(std::format_string<Args...> fmt, Args&&... args) {
    CURRENT_LOGGER->trace(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline static void Debug(std::format_string<Args...> fmt, Args&&... args) {
    CURRENT_LOGGER->debug(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline static void Info(std::format_string<Args...> fmt, Args&&... args) {
    CURRENT_LOGGER->info(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline static void Warn(std::format_string<Args...> fmt, Args&&... args) {
    CURRENT_LOGGER->warn(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline static void Error(std::format_string<Args...> fmt, Args&&... args) {
    CURRENT_LOGGER->error(fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline static void Critical(std::format_string<Args...> fmt, Args&&... args) {
    CURRENT_LOGGER->critical(fmt, std::forward<Args>(args)...);
  }
};

#endif  // !BECHAT_LOGGER_H_
