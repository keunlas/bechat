/**
 * @file request.h
 * @author Keunlas
 * @brief 请求消息类
 * @date 2026-08-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_PROTO_DISPATCHER_H_
#define BECHAT_PROTO_DISPATCHER_H_

#include <memory>
#include <optional>
#include <vector>

#include "bechat/proto/request.h"
#include "bechat/tlv/tlv_codec.h"
#include "bechat/utils/types.h"

[[deprecated]] struct Dispatcher {
 public:
  RequestResult Dispatch(SessionPtr session, TlvMessagePtr request);
};

#endif  // !BECHAT_PROTO_DISPATCHER_H_
