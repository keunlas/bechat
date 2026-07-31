#ifndef BECHAT_SESSION_H_
#define BECHAT_SESSION_H_

#include <asio.hpp>
#include <cstdint>
#include <memory>

#include "logger.h"

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}
  ~Session() { Log::Trace("Session {} has closed.", (void*)this); }

  void Start() { read_type(); }

 private:
  void read_type() {
    auto self(shared_from_this());
    asio::async_read(socket_, asio::buffer(&type_, sizeof(type_)),
                     [this, self](std::error_code ec, std::size_t) {
                       if (!ec) {
                         Log::Trace("type: 0x{:x}", type_);
                         read_length();
                       }
                     });
  }

  void read_length() {
    auto self(shared_from_this());
    asio::async_read(socket_, asio::buffer(&length_, sizeof(length_)),
                     [this, self](std::error_code ec, std::size_t) {
                       if (!ec) {
                         Log::Trace("length: {}", length_);
                         read_value(length_);
                       }
                     });
  }

  void read_value(uint16_t len) {
    auto self(shared_from_this());
    value_.resize(len);
    asio::async_read(socket_, asio::buffer(value_.data(), value_.size()),
                     [this, self](std::error_code ec, std::size_t) {
                       if (!ec) {
                         Log::Trace("value: {}", value_);
                         do_write();
                       }
                     });
  }

  void do_write() {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(value_),
                      [this, self](std::error_code ec, std::size_t /*length*/) {
                        if (!ec) {
                          Log::Trace("do_write: done");
                        }
                      });
  }

 private:
  asio::ip::tcp::socket socket_;

  uint16_t type_;
  uint16_t length_;
  std::string value_;
};

#endif  // !BECHAT_SESSION_H_
