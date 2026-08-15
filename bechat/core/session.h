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
  asio::strand<asio::any_io_executor> read_strand_;
  TlvMessage input_msg_{};

  asio::strand<asio::any_io_executor> write_strand_;
  std::queue<std::shared_ptr<TlvMessage>> write_queue_{};
  bool writing_{false};

  std::atomic_bool closing_{false};
};

#endif  // !BECHAT_CORE_SESSION_H_
