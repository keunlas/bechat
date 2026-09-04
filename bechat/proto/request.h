/**
 * @file request.h
 * @author Keunlas
 * @brief 请求相关类
 * @date 2026-08-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_PROTO_REQUEST_H_
#define BECHAT_PROTO_REQUEST_H_

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

struct RequestResult {
  /**
   * 应当先处理 `to_self_response`，再处理 `to_broadcast_push`
   */
  std::vector<TlvMessagePtr> to_self_response;   // 只发给当前连接
  std::vector<TlvMessagePtr> to_broadcast_push;  // 广播给所有在线连接
};

#endif  // !BECHAT_PROTO_REQUEST_H_
