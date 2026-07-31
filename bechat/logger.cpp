#include "logger.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#define LOGGER_NAME "bechat"

std::shared_ptr<spdlog::logger> Logger::AsyncConsoleLogger() {
  static std::shared_ptr<spdlog::logger> logger = [](const char* name) {
    auto l = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>(name);
    l->set_level(spdlog::level::trace);
    l->flush_on(spdlog::level::err);
    return l;
  }(LOGGER_NAME);
  return logger;
}
