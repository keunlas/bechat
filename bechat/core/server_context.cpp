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

RequestResult ServerContext::HandleRequest(SessionPtr session,
                                           TlvMessagePtr request) {}

void ServerContext::Broadcast(TlvMessagePtr push) {}
