#include "bechat/utils/logger.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "bechat/utils/config.h"

static constexpr auto kLoggerName{"bechat"};

static const auto kLoggerLevel{
    static_cast<spdlog::level::level_enum>(CFG_LOG_LEVEL)};

static const auto kLoggerFlushOn{
    static_cast<spdlog::level::level_enum>(CFG_LOG_FLUSH_ON)};

std::shared_ptr<spdlog::logger> Logger::AsyncConsoleLogger() {
  static std::shared_ptr<spdlog::logger> logger = []() {
    using spdlog::create_async;
    using spdlog::sinks::stdout_color_sink_mt;
    auto logger = create_async<stdout_color_sink_mt>(kLoggerName);
    logger->set_level(kLoggerLevel);
    logger->flush_on(kLoggerFlushOn);
    return logger;
  }();
  return logger;
}
