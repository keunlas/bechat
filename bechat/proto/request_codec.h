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

#include <memory>
#include <string_view>
#include <vector>

#include "bechat/tlv/message_tag.h"
#include "bechat/utils/types.h"

struct DecodedRequest {
  uint16_t tag;
  uint32_t request_id;
  std::string_view payload;  // 去掉 request_id 之后的剩余 value
};

class RequestCodec {
 public:
};

#endif  // !BECHAT_PROTO_REQUEST_CODEC_H_
