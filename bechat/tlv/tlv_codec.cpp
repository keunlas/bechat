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

TlvMessagePtr TlvCodec::MakeResponse(MessageTag::Resp::Type resp_tag,
                                     uint32_t request_id,
                                     StatusCode::Type status,
                                     std::string_view body) {
  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + body.size());
  ValueWriter writer(value);
  writer.WriteInt<uint32_t>(request_id);
  writer.WriteInt<uint16_t>(static_cast<uint16_t>(status));
  writer.WriteString(body);

  return std::make_shared<TlvMessage>(static_cast<uint16_t>(resp_tag),
                                      std::move(value));
}

TlvMessagePtr TlvCodec::MakePush(MessageTag::Push::Type push_tag,
                                 std::string_view body) {
  return std::make_shared<TlvMessage>(static_cast<uint16_t>(push_tag),
                                      std::move(std::string(body)));
}

TlvMessagePtr TlvCodec::MakeError(uint32_t request_id, uint16_t request_tag,
                                  StatusCode::Type status) {
  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t));
  ValueWriter writer(value);
  writer.WriteInt<uint32_t>(request_id);
  writer.WriteInt<uint16_t>(request_tag);
  writer.WriteInt<uint16_t>(static_cast<uint16_t>(status));

  return std::make_shared<TlvMessage>(
      static_cast<uint16_t>(MessageTag::Resp::Error), std::move(value));
}
