/**
 * @file server_context.cpp
 * @author Keunlas
 * @brief 服务器主上下文
 * @date 2026-08-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/core/server_context.h"

// ServerContext::ServerContext(IoThreads& io_threads):
//   io_threads_(io_threads) {}

RequestResult ServerContext::HandleRequest(SessionPtr session,
                                           TlvMessagePtr request) {
  // [TODO]
  RequestResult res;
  res.to_self_response.push_back(request);
  return res;
}

void ServerContext::Broadcast(SessionPtr session, TlvMessagePtr push) {
  // [TODO]
}
