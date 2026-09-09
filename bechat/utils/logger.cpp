#include "bechat/utils/logger.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

static constexpr auto kBechatLoggerName{"bechat"};
static constexpr auto kBechatLoggerLevel{spdlog::level::trace};
static constexpr auto kBechatLoggerFlushOn{spdlog::level::err};

std::shared_ptr<spdlog::logger> Logger::AsyncConsoleLogger() {
  static std::shared_ptr<spdlog::logger> logger = []() {
    using spdlog::create_async;
    using spdlog::sinks::stdout_color_sink_mt;
    auto logger = create_async<stdout_color_sink_mt>(kBechatLoggerName);
    logger->set_level(kBechatLoggerLevel);
    logger->flush_on(kBechatLoggerFlushOn);
    return logger;
  }();
  return logger;
}
