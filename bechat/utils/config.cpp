#include "bechat/utils/config.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

constexpr const char* DEFAULT_CFG_CONTENT{R"==(# SSL/TLS
ssl.cert  = /home/keunlas/MyCA/localhost.crt
ssl.key   = /home/keunlas/MyCA/localhost.key
ssl.dh    = /home/keunlas/MyCA/dh4096.pem

server.io_threads = 4
server.ip					= 127.0.0.1
server.port				= 35565
server.ssl_port		= 35566

# 0-trace | 1-debug | 2-info | 3-warn | 4-error | 5-critical
log.level				= 0
log.flush_on		= 4

)=="};

void Config::Init() {
  const std::string& cfg_path = CfgPath();
  std::filesystem::create_directories(cfg_path);

  auto cfg_filepath = CfgFilePath();
  if (!std::filesystem::exists(cfg_filepath)) {
    std::ofstream cfg(cfg_filepath);
    cfg.write(DEFAULT_CFG_CONTENT, std::strlen(DEFAULT_CFG_CONTENT));
  }
}

betools::Config& Config::Instance() {
  static betools::Config cfg = []() {
    Config::Init();
    betools::Config cfg(CfgFilePath());
    return cfg;
  }();
  return cfg;
}

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

const std::string& Config::CfgFilePath() {
  static std::string filepath{CfgPath() + "/bechat.cfg"};
  return filepath;
}
