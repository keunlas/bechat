#ifndef BECHAT_SESSION_H_
#define BECHAT_SESSION_H_

#include <asio.hpp>
#include <memory>

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}

  void Start() { do_read(); }

 private:
  void do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(
        asio::buffer(data_, max_length),
        [this, self](std::error_code ec, std::size_t length) {
          if (!ec) {
            do_write(length);
          }
        });
  }

  void do_write(std::size_t length) {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(data_, length),
                      [this, self](std::error_code ec, std::size_t /*length*/) {
                        if (!ec) {
                          do_read();
                        }
                      });
  }

 private:
  asio::ip::tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[1024];
};

#endif  // !BECHAT_SESSION_H_
