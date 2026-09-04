/**
 * @file tlv_codec.h
 * @author Keunlas
 * @brief 与 TlvMessage 相关的编解码操作
 * @date 2026-08-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_TLV_TLV_CODEC_H_
#define BECHAT_TLV_TLV_CODEC_H_

#include <bit>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "bechat/tlv/message_tag.h"
#include "bechat/tlv/status_code.h"
#include "bechat/tlv/tlv_message.h"
#include "bechat/tlv/tlv_value_reader.h"
#include "bechat/tlv/tlv_value_writer.h"

class TlvCodec {
 public:
  static TlvMessage MakeResponse(MessageTag::Resp::Type resp_tag,
                                 uint32_t request_id, StatusCode::Type status,
                                 std::string_view body = {});

  static TlvMessage MakeError(uint32_t request_id, uint16_t request_tag,
                              StatusCode::Type status);

  static TlvMessage MakePush(MessageTag::Push::Type push_tag,
                             std::string_view body = {});
};

#endif  // !BECHAT_TLV_TLV_CODEC_H_
