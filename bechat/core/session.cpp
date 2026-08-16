#include "bechat/core/session.h"

#include <endian.h>

#include "bechat/utils/logger.h"

Session::Session(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)),
      read_strand_(asio::make_strand(socket_.get_executor())),
      write_strand_(asio::make_strand(socket_.get_executor())) {}

Session::~Session() { INFO("Session {} has closed.", (void*)this); }

void Session::Start() {
  INFO("Session {} has started.", (void*)this);

  auto self(shared_from_this());
  asio::post(read_strand_, [this, self]() {
    if (closing_) return;
    read_tag();
  });
}

void Session::read_tag() {
  assert(read_strand_.running_in_this_thread());
  auto self(shared_from_this());
  asio::async_read(
      socket_, asio::buffer(input_msg_.mutable_tag(), sizeof(TlvMessage::TagT)),
      asio::bind_executor(read_strand_,
                          [this, self](std::error_code ec, std::size_t) {
                            if (closing_) return;
                            if (!ec) {
                              input_msg_.set_tag(be16toh(input_msg_.tag()));
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
      socket_,
      asio::buffer(input_msg_.mutable_length(), sizeof(TlvMessage::LengthT)),
      asio::bind_executor(
          read_strand_, [this, self](std::error_code ec, std::size_t) {
            if (closing_) return;
            if (!ec) {
              input_msg_.set_length(be16toh(input_msg_.length()));
              read_value(input_msg_.length());
            } else {
              handle_error(ec);
            }
          }));
}

void Session::read_value(uint16_t len) {
  assert(read_strand_.running_in_this_thread());
  assert(input_msg_.length() == len);
  (void)len;
  auto self(shared_from_this());
  input_msg_.mutable_value()->resize(input_msg_.length());
  asio::async_read(
      socket_,
      asio::buffer(input_msg_.mutable_value()->data(), input_msg_.length()),
      asio::bind_executor(read_strand_, [this, self](std::error_code ec,
                                                     std::size_t) {
        if (closing_) return;
        if (!ec) {
          TRACE("Session {} read a message: [0x{:x}][{}][{} bytes of value]",
                (void*)this, input_msg_.tag(), input_msg_.length(),
                input_msg_.value().size());

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
   * 这里已经完整的接收到了一条 message 到 input_msg_ 中，
   * 首先需要把当前的 input_msg_ 拷贝一份出来，
   * 然后在 write_strand_ 中进行该 message 的处理，
   * 处理完成后将序列化好的响应字符串添加到 write_queue_ 中。
   */

  auto self(shared_from_this());
  auto request_msg = std::make_shared<TlvMessage>(input_msg_);
  asio::post(write_strand_, [this, self, request_msg]() {
    if (closing_) return;

    {
      /**
       * [TODO]
       * 在这里进行 request_msg 的处理，
       * 目前相关的 Message 处理逻辑还没有实现，
       * 所以这里直接收到了什么东西就返回什么东西。
       * 直接把 request_msg 序列化后添加到 write_queue_ 中。
       */
      write_queue_.push(std::move(request_msg->SerializeToString()));
    }

    if (!writing_) start_writing();
  });
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
