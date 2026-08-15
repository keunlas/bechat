#ifndef BECHAT_CORE_SESSION_H_
#define BECHAT_CORE_SESSION_H_

#include <asio.hpp>
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

 private:
  asio::ip::tcp::socket socket_;
  asio::strand<asio::any_io_executor> read_strand_;
  TlvMessage input_msg_{};

  asio::strand<asio::any_io_executor> write_strand_;
  std::queue<std::shared_ptr<TlvMessage>> write_queue_{};
  bool writing_{false};
};

#endif  // !BECHAT_CORE_SESSION_H_
