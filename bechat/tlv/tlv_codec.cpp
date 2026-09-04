/**
 * @file tlv_codec.cpp
 * @author Keunlas
 * @brief 与 TlvMessage 相关的编解码操作
 * @date 2026-08-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bechat/tlv/tlv_codec.h"

#include <bit>
#include <cassert>

TlvMessage TlvCodec::MakeResponse(MessageTag::Resp::Type resp_tag,
                                  uint32_t request_id, StatusCode::Type status,
                                  std::string_view body) {
  TlvMessage response;
  response.set_tag(static_cast<uint16_t>(resp_tag));

  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + body.size());

  ValueWriter writer(value);
  writer.WriteInt<uint32_t>(request_id);
  writer.WriteInt<uint16_t>(static_cast<uint16_t>(status));
  writer.WriteString(body);

  response.set_value(value);
  return response;
}

TlvMessage TlvCodec::MakePush(MessageTag::Push::Type push_tag,
                              std::string_view body) {
  TlvMessage push;
  push.set_tag(static_cast<uint16_t>(push_tag));
  push.set_value(std::move(std::string(body)));
  return push;
}

TlvMessage TlvCodec::MakeError(uint32_t request_id, uint16_t request_tag,
                               StatusCode::Type status) {
  TlvMessage error;
  error.set_tag(static_cast<uint16_t>(MessageTag::Resp::Error));

  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t));

  ValueWriter writer(value);
  writer.WriteInt<uint32_t>(request_id);
  writer.WriteInt<uint16_t>(request_tag);
  writer.WriteInt<uint16_t>(static_cast<uint16_t>(status));

  error.set_value(value);
  return error;
}
