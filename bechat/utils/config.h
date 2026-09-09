#ifndef BECHAT_UTILS_CONFIG_H_
#define BECHAT_UTILS_CONFIG_H_

#include <betools/config.hpp>
#include <cstdint>
#include <string>

class Config {
 public:
  static const std::string& CfgPath();
  static betools::Config& Instance();
};

#define CFG_SSL_CERT Config::Instance().GetValue("ssl.cert")
#define CFG_SSL_KEY Config::Instance().GetValue("ssl.key")
#define CFG_SSL_DH Config::Instance().GetValue("ssl.dh")

#define CFG_SERVER_IO_THREADS Config::Instance().GetAs<int>("server.io_threads")
#define CFG_SERVER_IP Config::Instance().GetValue("server.ip")
#define CFG_SERVER_PORT Config::Instance().GetAs<uint16_t>("server.port")
#define CFG_SERVER_SSL_PORT Config::Instance().GetAs<uint16_t>("server.ssl_port")

#define CFG_LOG_LEVEL Config::Instance().GetAs<int>("log.level")
#define CFG_LOG_FLUSH_ON Config::Instance().GetAs<int>("log.flush_on")

#endif  // !BECHAT_UTILS_CONFIG_H_
