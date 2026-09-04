/**
 * @file message_tag.h
 * @author Keunlas
 * @brief 消息 Tag 的枚举值
 * @date 2026-08-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_TLV_MESSAGE_TAG_H_
#define BECHAT_TLV_MESSAGE_TAG_H_

#include <cstdint>

class MessageTag {
 public:
  struct Req {
    enum Type : uint16_t {
      Register = 0x0001,  // 用户注册
      Login,              // 用户登录
      GetHistory,         // 获取历史聊天消息
      GetOnlineUsers,     // 获取在线用户列表
      SendMessage,        // 发送聊天消息
    };
  };

  struct Resp {
    enum Type : uint16_t {
      Error = 0x8000,     // 通用错误响应
      Register = 0x8001,  // 注册响应
      Login,              // 登录响应
      GetHistory,         // 历史消息响应
      GetOnlineUsers,     // 在线用户响应
      SendMessage,        // 发送消息响应
    };
  };

  struct Push {
    enum Type : uint16_t {
      NewMessage = 0x8101,  // 新聊天消息推送
      UserJoined,           // 用户加入聊天室推送
    };
  };

 public:
  inline static bool IsValidReqTag(uint16_t tag) {
    switch (tag) {
      case Req::Register:
      case Req::Login:
      case Req::GetHistory:
      case Req::GetOnlineUsers:
      case Req::SendMessage:
        return true;
      default:
        return false;
    }
  }

  inline static bool IsValidRespTag(uint16_t tag) {
    switch (tag) {
      case Resp::Register:
      case Resp::Login:
      case Resp::GetHistory:
      case Resp::GetOnlineUsers:
      case Resp::SendMessage:
        return true;
      default:
        return false;
    }
  }

  inline static bool IsValidPushTag(uint16_t tag) {
    switch (tag) {
      case Push::NewMessage:
      case Push::UserJoined:
        return true;
      default:
        return false;
    }
  }

  /// @brief 相对应的请求和响应的 Tag 互转
  inline static uint16_t CorrespondingConvert(uint16_t tag) {
    return tag ^ 0x8000;
  }
};

#endif  // !BECHAT_TLV_MESSAGE_TAG_H_
