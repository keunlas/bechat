#ifndef BECHAT_CORE_SESSION_H_
#define BECHAT_CORE_SESSION_H_

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <queue>

#include "bechat/tlv/tlv_message.h"

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket);
  ~Session();

  void Start();

 private:
  void read_tag();
  void read_length();
  void read_value(uint16_t len);

  void write_resp();
  void start_writing();

  void handle_error(const std::error_code& ec);

 private:
  asio::ip::tcp::socket socket_;

  // 当出现错误或者异常时，置位 closing_
  std::atomic_bool closing_{false};

  // 读 strand 用来进行读操作
  asio::strand<asio::any_io_executor> read_strand_;
  // 只在 read_strand_ 中进行修改
  TlvMessage input_msg_{};

  // 写 strand 用来进行写操作
  asio::strand<asio::any_io_executor> write_strand_;
  // 只在 write_strand_ 中进行修改
  std::queue<std::string> write_queue_{};
  // 只在 write_strand_ 中进行修改
  bool writing_{false};
};

#endif  // !BECHAT_CORE_SESSION_H_
