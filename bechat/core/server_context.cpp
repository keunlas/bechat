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

#include "bechat/proto/request_codec.h"
#include "bechat/proto/request_message.h"
#include "bechat/tlv/tlv_codec.h"
#include "bechat/utils/logger.h"

ServerContext::ServerContext(IoThreads& io_threads) : io_threads_(io_threads) {}

void ServerContext::HandleRequest(SessionPtr session,
                                  RequestMessagePtr request) {
  asio::post(io_threads_.GetIoContext(), [this, session, request]() {
    try {
      // Tag 不对则返回 StatusCode::MalformedPacket
      if (!MessageTag::IsValidReqTag(request->tag())) {
        session->Send(TlvCodec::MakeError(request->request_id(), request->tag(),
                                          StatusCode::MalformedPacket));
        return;
      }

      // RequestId 不存在则返回 StatusCode::InvalidRequestId
      if (request->request_id() == 0U) {
        session->Send(TlvCodec::MakeError(request->request_id(), request->tag(),
                                          StatusCode::InvalidRequestId));
        return;
      }

      // [TODO] 解析并处理 request 的业务逻辑
      session->Send(TlvCodec::MakeResponse(
          static_cast<MessageTag::Resp::Type>(
              MessageTag::CorrespondingConvert(request->tag())),
          request->request_id(), StatusCode::Ok, "test OK respose"));

    } catch (const std::exception& e) {
      ERROR("Session {} failed to handle request: {}", (void*)session.get(),
            e.what());
    } catch (...) {
      ERROR("Session {} failed to handle request: unknow error",
            (void*)session.get());
    }
  });
}

void ServerContext::Broadcast(SessionPtr session, TlvMessagePtr push) {
  // [TODO]
}
