/**
 * @file logger.cpp
 * @author Keunlas
 * @brief 对 spdlog 的封装
 * @date 2026-08-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/utils/logger.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>

namespace {
constexpr const char* kBechatLoggerName{"bechat"};
}  // namespace

std::shared_ptr<spdlog::logger> Logger::AsyncConsoleLogger() {
  using spdlog::sinks::stdout_color_sink_mt;
  static std::shared_ptr<spdlog::logger> logger = [](const char* name) {
    auto logger = spdlog::create_async<stdout_color_sink_mt>(name);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::err);
    return logger;
  }(kBechatLoggerName);
  return logger;
}
