/**
 * @file types.h
 * @author Keunlas
 * @brief 一些特定的类型
 * @date 2026-08-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_UTILS_TYPES_H_
#define BECHAT_UTILS_TYPES_H_

#include <memory>

class Session;

using SessionPtr = std::shared_ptr<Session>;

class TlvMessage;

using TlvMessagePtr = std::shared_ptr<TlvMessage>;

#endif  // !BECHAT_UTILS_TYPES_H_
