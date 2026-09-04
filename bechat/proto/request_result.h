/**
 * @file request_result.h
 * @author Keunlas
 * @brief 请求解析结果类
 * @date 2026-08-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_PROTO_REQUEST_RESULT_H_
#define BECHAT_PROTO_REQUEST_RESULT_H_

#include <memory>
#include <string_view>
#include <vector>

#include "bechat/tlv/message_tag.h"
#include "bechat/utils/types.h"

struct RequestResult {
  /**
   * 注意：
   *   原则上先处理 `to_self_response`，再处理 `to_broadcast_push`
   */

  std::vector<TlvMessagePtr> to_self_response;   // 只发给当前连接
  std::vector<TlvMessagePtr> to_broadcast_push;  // 广播给所有在线连接
};

#endif  // !BECHAT_PROTO_REQUEST_RESULT_H_
