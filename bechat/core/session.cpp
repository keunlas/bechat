#include "bechat/core/session.h"

#include <bit>
#include <ranges>

#include "bechat/core/server_context.h"
#include "bechat/proto/request_message.h"
#include "bechat/utils/logger.h"

Session::Session(asio::ip::tcp::socket socket, ServerContext& context)
    : socket_(std::move(socket)),
      server_context_(context),
      read_strand_(asio::make_strand(socket_.get_executor())),
      write_strand_(asio::make_strand(socket_.get_executor())) {}

Session::~Session() { INFO("Session {} has closed.", (void*)this); }

void Session::Start() {
  INFO("Session {} has started.", (void*)this);

  auto self(shared_from_this());
  asio::post(read_strand_, [this, self]() {
    try {
      if (closing_) return;
      read_tag();
    } catch (const std::exception& e) {
      ERROR("Session {} failed to start: {}", (void*)this, e.what());
    } catch (...) {
      ERROR("Session {} failed to start: unknow error", (void*)this);
    }
  });
}

void Session::Send(TlvMessagePtr msg) {
  auto self(shared_from_this());
  asio::post(write_strand_, [this, self, msg]() {
    try {
      if (closing_) return;
      write_queue_.push(std::move(msg->SerializeToString()));
      if (!writing_) start_writing();
    } catch (const std::exception& e) {
      ERROR("Session {} failed to send msg: {}", (void*)this, e.what());
    } catch (...) {
      ERROR("Session {} failed to send msg: unknow error", (void*)this);
    }
  });
}

void Session::Send(std::vector<TlvMessagePtr> msgs) {
  auto self(shared_from_this());
  asio::post(write_strand_, [this, self, msgs = std::move(msgs)]() {
    try {
      if (closing_) return;
      for (auto&& msg : msgs) {
        write_queue_.push(std::move(msg->SerializeToString()));
      }
      if (!writing_) start_writing();
    } catch (const std::exception& e) {
      ERROR("Session {} failed to send msgs: {}", (void*)this, e.what());
    } catch (...) {
      ERROR("Session {} failed to send msgs: unknow error", (void*)this);
    }
  });
}

void Session::read_tag() {
  assert(read_strand_.running_in_this_thread());
  auto self(shared_from_this());
  asio::async_read(
      socket_, asio::buffer(&current_msg_tag_, sizeof(current_msg_tag_)),
      asio::bind_executor(
          read_strand_, [this, self](std::error_code ec, std::size_t) {
            if (closing_) return;
            if (!ec) {
              if (std::endian::native != std::endian::big) {
                current_msg_tag_ = std::byteswap(current_msg_tag_);
              }
              read_length();
            } else {
              handle_error(ec);
            }
          }));
}

void Session::read_length() {
  assert(read_strand_.running_in_this_thread());
  auto self(shared_from_this());
  asio::async_read(
      socket_, asio::buffer(&current_msg_length_, sizeof(current_msg_length_)),
      asio::bind_executor(
          read_strand_, [this, self](std::error_code ec, std::size_t) {
            if (closing_) return;
            if (!ec) {
              if (std::endian::native != std::endian::big) {
                current_msg_length_ = std::byteswap(current_msg_length_);
              }
              read_value(current_msg_length_);
            } else {
              handle_error(ec);
            }
          }));
}

void Session::read_value(uint16_t len) {
  assert(read_strand_.running_in_this_thread());
  assert(current_msg_length_ == len);
  (void)len;
  auto self(shared_from_this());
  current_msg_value_.resize(len);
  asio::async_read(
      socket_,
      asio::buffer(current_msg_value_.data(), current_msg_value_.size()),
      asio::bind_executor(read_strand_, [this, self](std::error_code ec,
                                                     std::size_t) {
        if (closing_) return;
        if (!ec) {
          TRACE("Session {} read a message: [0x{:x}][{}][{} bytes of value]",
                (void*)this, current_msg_tag_, current_msg_length_,
                current_msg_value_.size());

          on_read_completed();
          read_tag();  // TCP 全双工通信，读写可同时进行
        } else {
          handle_error(ec);
        }
      }));
}

void Session::on_read_completed() {
  assert(read_strand_.running_in_this_thread());

  /**
   * 这里已经完整的接收到了一条 message 到 current_msg_* 中，
   * 首先需要把当前的 message 拷贝一份出来。
   */

  auto self(shared_from_this());
  auto request_msg =
      std::make_shared<RequestMessage>(current_msg_tag_, current_msg_value_);

  /**
   * 这里不再直接对 request_msg 进行处理，
   * 而是把 request_msg 和 self 交给 server_context_ 进行处理，
   * server_context_ 会调用 Send 接口发送处理好的请求。
   */

  server_context_.HandleRequest(self, request_msg);  // 异步接口
}

void Session::start_writing() {
  assert(write_strand_.running_in_this_thread());

  if (write_queue_.empty() || closing_) {
    writing_ = false;
    return;
  }

  writing_ = true;
  auto self(shared_from_this());
  const std::string& resp_msg_str = write_queue_.front();

  asio::async_write(
      socket_, asio::buffer(resp_msg_str),
      asio::bind_executor(write_strand_,
                          [this, self, resp_msg_str_size = resp_msg_str.size()](
                              std::error_code ec, std::size_t writed_length) {
                            (void)writed_length;
                            write_queue_.pop();
                            writing_ = false;

                            if (!ec) {
                              assert(resp_msg_str_size == writed_length);
                              TRACE("writing a resp: done");
                              start_writing();
                            } else {
                              handle_error(ec);
                            }
                          }));
}

void Session::handle_error(const std::error_code& ec) {
  assert(write_strand_.running_in_this_thread() ||
         read_strand_.running_in_this_thread());

  if (closing_.exchange(true)) return;

  ERROR("Session {} is shutting down due to error: {}", (void*)this,
        ec.message());

  handle_close();
}

void Session::handle_close() {
  assert(write_strand_.running_in_this_thread() ||
         read_strand_.running_in_this_thread());

  auto self = shared_from_this();

  // 首先使用 cancel 同时取消挂起的 async_read 和 async_write
  // 然后使用 shutdown 关闭 socket 的读端和写端
  // 最后使用 close 关闭 socket
  asio::post(write_strand_, [this, self]() {
    std::error_code ignored_ec;
    socket_.cancel(ignored_ec);
    socket_.shutdown(asio::socket_base::shutdown_both, ignored_ec);
    socket_.close(ignored_ec);
  });
}
