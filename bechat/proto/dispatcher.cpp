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

  // Tag 不对则返回 StatusCode::MalformedPacket
  if (!MessageTag::IsValidReqTag(tag)) {
    RequestResult res;
    res.to_self_response.emplace_back(std::make_shared<TlvMessage>(std::move(
        TlvCodec::MakeError(request_id.has_value() ? request_id.value() : 0,
                            tag, StatusCode::MalformedPacket))));
    return res;
  }

  // RequestId 不存在则返回 StatusCode::InvalidParameter
  if (!request_id.has_value()) {
    RequestResult res;
    res.to_self_response.emplace_back(std::make_shared<TlvMessage>(
        std::move(TlvCodec::MakeError(0, tag, StatusCode::InvalidParameter))));
    return res;
  }

  // decoded 就是已经单独提取出 tag, request_id 和 payload 的请求
  DecodedRequest decoded;
  decoded.payload = TlvCodec::PayloadAfterRequestId(request->value());
  decoded.request_id = request_id.value();
  decoded.tag = tag;

  // [TODO] 处理 decoded
  RequestResult res;
  res.to_self_response.emplace_back(
      std::make_shared<TlvMessage>(std::move(TlvCodec::MakeResponse(
          MessageTag::ReqTag2Resp(decoded.tag), decoded.request_id,
          StatusCode::Ok, "'OK message for test'"))));
  return res;
}
