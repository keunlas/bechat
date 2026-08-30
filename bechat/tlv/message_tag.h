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
      Register = 0x0001,        // 用户注册
      Login = 0x0002,           // 用户登录
      GetHistory = 0x0003,      // 获取历史聊天消息
      GetOnlineUsers = 0x0004,  // 获取在线用户列表
      SendMessage = 0x0005,     // 发送聊天消息
    };
  };

  struct Resp {
    enum Type : uint16_t {
      Error = 0x8000,           // 通用错误响应
      Register = 0x8001,        // 注册响应
      Login = 0x8002,           // 登录响应
      GetHistory = 0x8003,      // 历史消息响应
      GetOnlineUsers = 0x8004,  // 在线用户响应
      SendMessage = 0x8005,     // 发送消息响应
    };
  };

  struct Push {
    enum Type : uint16_t {
      NewMessage = 0x8101,  // 新聊天消息推送
      UserJoined = 0x8102,  // 用户加入聊天室推送
    };
  };

 public:
  inline static bool IsValidReqTag(Req::Type tag) {
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

  inline static Resp::Type ReqTag2Resp(Req::Type tag) {
    switch (tag) {
      case Req::Register:
        return Resp::Register;
      case Req::Login:
        return Resp::Login;
      case Req::GetHistory:
        return Resp::GetHistory;
      case Req::GetOnlineUsers:
        return Resp::GetOnlineUsers;
      case Req::SendMessage:
        return Resp::SendMessage;
      default:
        return Resp::Error;
    }
  }
};

#endif  // !BECHAT_TLV_MESSAGE_TAG_H_
