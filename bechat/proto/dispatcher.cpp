/**
 * @file request.cpp
 * @author Keunlas
 * @brief 请求消息类
 * @date 2026-08-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/proto/dispatcher.h"

RequestResult Dispatcher::Dispatch(SessionPtr session, TlvMessagePtr request) {
  auto tag = static_cast<MessageTag::Req::Type>(request->tag());
  auto request_id = TlvCodec::PeekRequestId(request->value());
  if (!MessageTag::IsValidReqTag(tag) || !request_id.has_value()) {
    // tag 必须合法且 request_id 必须存在
    RequestResult res;
    res.to_self_response.emplace_back(std::make_shared<TlvMessage>(
        std::move(TlvCodec::MakeError(0, tag, StatusCode::MalformedPacket))));
    return res;
  }

  DecodedRequest decoded;
  decoded.payload = TlvCodec::PayloadAfterRequestId(request->value());
  decoded.request_id = request_id.value();
  decoded.tag = tag;

  // [TODO]
  RequestResult res;
  res.to_self_response.emplace_back(
      std::make_shared<TlvMessage>(std::move(TlvCodec::MakeResponse(
          MessageTag::ReqTag2Resp(decoded.tag), decoded.request_id,
          StatusCode::Ok, "'OK message for test'"))));
  return res;
}
