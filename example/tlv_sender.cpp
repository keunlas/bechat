/**
 * @file tlv_sender.cpp
 * @author Keunlas
 * @brief 用于测试的 TLV 客户端：连接服务器并逐行发送 TlvMessage
 * @date 2026-08-17
 *
 * @copyright Copyright (c) 2026
 *
 * 用法:
 *   tlv_sender <host> <port>
 *
 * 连接后从标准输入读取一行，格式为:
 *   <tag(hex)> <length(dec)> <value>
 *
 * 示例:
 *   0x1234 5 ABCDE          -> Tag=0x1234, Length=5,  Value="ABCDE"
 *   0x0002 0                -> Tag=0x0002, Length=0,  空 Value
 *
 * 说明:
 *   - tag 以十六进制解析（支持 0x 前缀）；
 *   - length 以十进制解析，表示要发送的 Value 字节数；
 *   - value 取输入中剩余部分的「前 length 个字节」，不足 length 时报错并跳过。
 *
 * 发送成功后等待并打印服务器返回的 TLV 响应（超时 3 秒）。
 */

#include <asio.hpp>

#include <endian.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "bechat/tlv/tlv_message.h"

namespace {

constexpr int kReadTimeoutMs = 3000;

// 等待 socket 可读，最多 timeout_ms 毫秒；可读返回 true，超时/出错返回 false。
bool WaitReadable(asio::ip::tcp::socket& socket, int timeout_ms) {
  struct pollfd pfd {};
  pfd.fd = socket.native_handle();
  pfd.events = POLLIN;
  int ret = ::poll(&pfd, 1, timeout_ms);
  if (ret <= 0) {
    return false;
  }
  return (pfd.revents & POLLIN) != 0;
}

// 从 socket 上读取一个完整的 TLV；成功返回 true，失败/超时返回 false。
bool ReadOneTlv(asio::ip::tcp::socket& socket, TlvMessage& msg,
                int timeout_ms) {
  if (!WaitReadable(socket, timeout_ms)) {
    std::cout << "  (no response within " << timeout_ms << " ms)\n";
    return false;
  }

  std::error_code ec;
  asio::read(socket,
             asio::buffer(msg.mutable_tag(), sizeof(TlvMessage::TagT)), ec);
  if (ec) {
    std::cerr << "  read tag failed: " << ec.message() << "\n";
    return false;
  }
  msg.set_tag(be16toh(msg.tag()));

  asio::read(socket,
             asio::buffer(msg.mutable_length(), sizeof(TlvMessage::LengthT)),
             ec);
  if (ec) {
    std::cerr << "  read length failed: " << ec.message() << "\n";
    return false;
  }
  msg.set_length(be16toh(msg.length()));

  msg.mutable_value()->resize(msg.length());
  asio::read(socket, asio::buffer(msg.mutable_value()->data(), msg.length()),
             ec);
  if (ec) {
    std::cerr << "  read value failed: " << ec.message() << "\n";
    return false;
  }
  return true;
}

// 打印一个 TLV；可打印字符原样输出，不可打印字符输出为 \xNN。
void PrintTlv(const TlvMessage& msg) {
  std::cout << "  resp: tag=0x" << std::hex << msg.tag() << std::dec
            << " length=" << msg.length() << " value=\"";
  for (unsigned char c : msg.value()) {
    if (c >= 0x20 && c <= 0x7e) {
      std::cout << static_cast<char>(c);
    } else {
      std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(c) << std::dec << std::setfill(' ');
    }
  }
  std::cout << "\"\n";
}

// 解析一行输入，成功返回 true 并填充 msg。
bool ParseLine(const std::string& line, TlvMessage& msg) {
  std::istringstream iss(line);
  std::string tag_token;
  std::string len_token;
  if (!(iss >> tag_token >> len_token)) {
    std::cerr << "  bad input: expected \"<tag(hex)> <length(dec)> <value>\"\n";
    return false;
  }

  uint32_t tag = 0;
  uint32_t len = 0;
  try {
    tag = static_cast<uint32_t>(std::stoul(tag_token, nullptr, 16));
    len = static_cast<uint32_t>(std::stoul(len_token, nullptr, 10));
  } catch (const std::exception&) {
    std::cerr << "  bad input: cannot parse tag/length\n";
    return false;
  }
  if (tag > 0xFFFFu) {
    std::cerr << "  bad input: tag out of range (0x0000 ~ 0xFFFF)\n";
    return false;
  }
  if (len > 0xFFFFu) {
    std::cerr << "  bad input: length out of range (0 ~ 65535)\n";
    return false;
  }

  // 剩余部分即 Value；去掉开头的空白。
  std::string value;
  std::getline(iss, value);
  std::size_t start = value.find_first_not_of(" \t");
  if (start != std::string::npos) {
    value = value.substr(start);
  } else {
    value.clear();
  }

  if (value.size() < len) {
    std::cerr << "  bad input: value too short, expected " << len
              << " bytes but got " << value.size() << "\n";
    return false;
  }
  if (value.size() > len) {
    std::cout << "  (note: value truncated from " << value.size()
              << " to " << len << " bytes)\n";
    value.resize(len);
  }

  msg.set_tag(static_cast<TlvMessage::TagT>(tag));
  msg.set_length(static_cast<TlvMessage::LengthT>(len));
  msg.set_value(value);
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <host> <port>\n";
    std::cerr << "Example: " << argv[0] << " 127.0.0.1 35565\n";
    return 1;
  }

  try {
    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);

    asio::ip::tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(argv[1], argv[2]);
    asio::connect(socket, endpoints);

    std::cout << "Connected to " << argv[1] << ":" << argv[2] << "\n";
    std::cout << "Input format: <tag(hex)> <length(dec)> <value>\n";
    std::cout << "Example: 0x1234 5 ABCDE\n";
    std::cout << "Empty line to skip, Ctrl+D to exit.\n";

    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.empty()) {
        continue;
      }

      TlvMessage msg;
      if (!ParseLine(line, msg)) {
        continue;
      }

      std::string serialized = msg.SerializeToString();
      asio::write(socket, asio::buffer(serialized));
      std::cout << "  sent:   tag=0x" << std::hex << msg.tag() << std::dec
                << " length=" << msg.length() << " value=\"" << msg.value()
                << "\"\n";

      TlvMessage resp;
      if (ReadOneTlv(socket, resp, kReadTimeoutMs)) {
        PrintTlv(resp);
      }
    }

    std::error_code ignored_ec;
    socket.shutdown(asio::socket_base::shutdown_both, ignored_ec);
    socket.close(ignored_ec);
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

