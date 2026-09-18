#include "bechat/utils/global.h"

#include <sodium.h>

#include <stdexcept>

void Global::Init() {
  //  初始化 libsodium
  if (sodium_init() < 0) {
    throw std::runtime_error("libsodium initialization failed!");
  }
}
