#include "bechat/utils/config.h"

betools::Config& Config::Instance() {
  static betools::Config cfg = []() {
    const char* home = std::getenv("HOME");
    std::string config_path = std::string(home) + "/.config/bechat/bechat.cfg";
    betools::Config cfg(config_path);
    return cfg;
  }();
  return cfg;
}
