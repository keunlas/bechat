/**
 * @file request_codec.h
 * @author Keunlas
 * @brief 解析请求相关类
 * @date 2026-09-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_PROTO_REQUEST_CODEC_H_
#define BECHAT_PROTO_REQUEST_CODEC_H_

#include <expected>
#include <memory>
#include <string_view>
#include <variant>
#include <vector>

#include "bechat/proto/request_params.h"
#include "bechat/tlv/message_tag.h"
#include "bechat/tlv/status_code.h"
#include "bechat/utils/types.h"

using DecodeOutcome = std::expected<RequestParams, StatusCode::Type>;

class RequestCodec {
 public:
  static DecodeOutcome Decode(uint16_t tag, std::string_view payload) {
    DecodeOutcome outcome;

    // switch (tag) {
    //   case 1:
    //     /* code */
    //     break;

    //   default:
    //     break;
    // }

    return outcome;
  }
};

#endif  // !BECHAT_PROTO_REQUEST_CODEC_H_
