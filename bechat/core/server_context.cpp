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

#include "bechat/proto/request.h"
#include "bechat/tlv/tlv_codec.h"

ServerContext::ServerContext(IoThreads& io_threads) : io_threads_(io_threads) {}

void ServerContext::HandleRequest(SessionPtr session, TlvMessagePtr request) {
  asio::post(io_threads_.GetIoContext(), [session, request]() {
    auto tag = static_cast<MessageTag::Req::Type>(request->tag());
    auto request_id = TlvCodec::PeekRequestId(request->value());

    // Tag 不对则返回 StatusCode::MalformedPacket
    if (!MessageTag::IsValidReqTag(tag)) {
      session->Send(std::make_shared<TlvMessage>(std::move(
          TlvCodec::MakeError(request_id.has_value() ? request_id.value() : 0,
                              tag, StatusCode::MalformedPacket))));
      return;
    }

    // RequestId 不存在则返回 StatusCode::InvalidParameter
    if (!request_id.has_value()) {
      session->Send(std::make_shared<TlvMessage>(std::move(
          TlvCodec::MakeError(0, tag, StatusCode::InvalidParameter))));
      return;
    }

    // decoded 就是已经单独提取出 tag, request_id 和 payload 的请求
    DecodedRequest decoded;
    decoded.payload = TlvCodec::PayloadAfterRequestId(request->value());
    decoded.request_id = request_id.value();
    decoded.tag = tag;

    // [TODO] 处理 decoded
    session->Send(std::make_shared<TlvMessage>(std::move(TlvCodec::MakeResponse(
        MessageTag::ReqTag2Resp(decoded.tag), decoded.request_id,
        StatusCode::Ok, "'OK message for test'"))));
    return;
  });
}

void ServerContext::Broadcast(SessionPtr session, TlvMessagePtr push) {
  // [TODO]
}
