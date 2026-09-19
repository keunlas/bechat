/**
 * @file tlv_sender.cpp
 * @author Keunlas
 * @brief 简单的 TLV 消息发送工具
 * @date 2026-09-10
 *
 * @copyright Copyright (c) 2026
 *
 * 用法：tlv_sender [host] [port]，默认连接 localhost:35565
 *
 * 启动后每输入一行，就向服务端发送一条 TLV 消息，然后接收一条响应并回显，
 * 输入格式为：
 *
 *   <tag> <length> <value>
 *
 * tag 为十六进制（可带 0x），length 为十进制长度，value 为内容，例如：
 *
 *   0x11a3 5 12345
 *   11a3 5 12345
 *
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

/// @brief 连接 host:port，失败返回 -1
static int Connect(const char* host, const char* port) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* result = nullptr;
  if (getaddrinfo(host, port, &hints, &result) != 0) return -1;

  int fd = -1;
  for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }

  freeaddrinfo(result);
  return fd;
}

/// @brief 把字节串转成十六进制字符串，方便回显
static std::string ToHex(std::string_view data) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(data.size() * 2);
  for (unsigned char byte : data) {
    result.push_back(kHex[byte >> 4]);
    result.push_back(kHex[byte & 0x0f]);
  }
  return result;
}

int main(int argc, char* argv[]) {
  const char* host = argc > 1 ? argv[1] : "localhost";
  const char* port = argc > 2 ? argv[2] : "35565";

  int fd = Connect(host, port);
  if (fd < 0) {
    std::cerr << "connect " << host << ":" << port << " failed\n";
    return 1;
  }

  // 每输入一行发送一条消息，输入结束(EOF)后退出
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream iss(line);
    std::string tag_str;
    std::string length_str;
    if (!(iss >> tag_str >> length_str)) continue;

    auto tag =
        static_cast<uint16_t>(std::strtoul(tag_str.c_str(), nullptr, 16));
    auto length =
        static_cast<uint16_t>(std::strtoul(length_str.c_str(), nullptr, 10));

    // 剩下的全部内容作为 value
    std::string value;
    std::getline(iss >> std::ws, value);

    // 组装 TLV 消息：tag(u16 大端) + length(u16 大端) + value
    std::string message;
    auto be_tag = htons(tag);
    auto be_length = htons(length);
    message.append(reinterpret_cast<const char*>(&be_tag), sizeof(be_tag));
    message.append(reinterpret_cast<const char*>(&be_length),
                   sizeof(be_length));
    message.append(value);

    if (send(fd, message.data(), message.size(), 0) !=
        static_cast<ssize_t>(message.size())) {
      std::cerr << "send failed\n";
      break;
    }

    // 接收一条响应：tag(u16 大端) + length(u16 大端) + value
    uint16_t be_resp_tag = 0;
    uint16_t be_resp_length = 0;
    if (recv(fd, &be_resp_tag, sizeof(be_resp_tag), MSG_WAITALL) !=
            static_cast<ssize_t>(sizeof(be_resp_tag)) ||
        recv(fd, &be_resp_length, sizeof(be_resp_length), MSG_WAITALL) !=
            static_cast<ssize_t>(sizeof(be_resp_length))) {
      std::cerr << "recv failed\n";
      break;
    }

    auto resp_tag = ntohs(be_resp_tag);
    auto resp_length = ntohs(be_resp_length);

    std::string resp_value(resp_length, '\0');
    if (resp_length > 0 &&
        recv(fd, resp_value.data(), resp_length, MSG_WAITALL) !=
            static_cast<ssize_t>(resp_length)) {
      std::cerr << "recv failed\n";
      break;
    }

    std::cout << "recv: tag=0x" << std::hex << resp_tag << std::dec
              << " length=" << resp_length << " hex_value=" << ToHex(resp_value)
              << " value=" << resp_value << "\n";
  }

  close(fd);
  return 0;
}
