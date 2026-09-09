#include "bechat/utils/config.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

const std::string& Config::CfgPath() {
  static std::string path = []() {
    const char* home = std::getenv("HOME");
    if (!home) {
      std::cerr << "Cannot find $HOME directory." << '\n';
      std::abort();
    }
    auto path = std::string(home) + "/.config/bechat";
    return path;
  }();
  return path;
}

betools::Config& Config::Instance() {
  static betools::Config cfg = []() {
    std::string config_path = CfgPath() + "/bechat.cfg";
    betools::Config cfg(config_path);
    return cfg;
  }();
  return cfg;
}
