#ifndef BECHAT_CORE_SESSION_H_
#define BECHAT_CORE_SESSION_H_

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "bechat/core/server_context.h"
#include "bechat/core/session_handle.h"
#include "bechat/utils/logger.h"

template <typename T>
struct IsSslStreamT : std::false_type {};

template <typename Socket>
struct IsSslStreamT<asio::ssl::stream<Socket>> : std::true_type {};

template <typename T>
inline constexpr bool IsSslStream =
    IsSslStreamT<std::remove_cv_t<std::remove_reference_t<T>>>::value;

/**
 * @brief 生成 Session 的自增 id
 *
 * 定义在非模板的上下文里，保证 NosslSession 和 SslSession 共用同一个计数器；
 * 否则模板的每个实例化都会有一份自己的静态变量。
 *
 * id 从 1 开始，只保证同时存活的 Session 之间不重复。发完 2^64 个之后计数器
 * 会回绕（回绕时会先发出一次 0），然后重新从 1 开始——这个量级现实中不可能
 * 达到，但如果以后 id 需要跨进程/永久唯一，得换成带启动时间戳或者其他的方案。
 *
 * @return uint64_t 从 1 开始的自增 id
 */
inline uint64_t NextSessionId() {
  static std::atomic<uint64_t> next_id{0};
  return next_id.fetch_add(1, std::memory_order_relaxed) + 1;
}

/**
 * @brief Session 类
 *
 * @tparam Socket 可以是 `asio::ip::tcp::socket`
 * 或者 `asio::ssl::stream<asio::ip::tcp::socket>` 等其他流式 socket
 */
template <typename Socket>
class Session : public std::enable_shared_from_this<Session<Socket>>,
                public SessionHandle {
 public:
  // TLS 挥手等待对方 close_notify 的超时时间
  static constexpr auto kShutdownTimeout{std::chrono::seconds(3)};

  // TLV 协议各字段大小
  static constexpr uint16_t kTagSize{sizeof(uint16_t)};
  static constexpr uint16_t kLengthSize{sizeof(uint16_t)};
  static constexpr uint16_t kHeaderSize{kTagSize + kLengthSize};
  static constexpr uint16_t kMaxValueSize{std::numeric_limits<uint16_t>::max()};

  // 每次读取的块的大小
  static constexpr uint16_t kChunkSize{4096U};

  // 一次发送的数据量的上限（发送队列中连续的小数据会被合并成一批发送）
  static constexpr std::size_t kMaxBatchSize{4096U};

  // kMaxPayloadSize 必须比 kMaxValueSize 小才会起作用
  static constexpr uint16_t kMaxPayloadSize{kMaxValueSize};

 public:
  explicit Session(ServerContexts& server_contexts, Socket socket)
      : server_contexts_(server_contexts),
        socket_(std::move(socket)),
        strand_(asio::make_strand(socket_.get_executor())),
        streambuf_(kTagSize + kLengthSize + kMaxValueSize),
        shutdown_timer_(socket_.get_executor()) {}

  /**
   * @brief Session 启动的入口
   *
   */
  void Start() {
    if constexpr (IsSslStream<Socket>) {
      INFO("Session#{} is a SslSession", Id());
      ssl_handshake();
    } else {
      INFO("Session#{} is a NosslSession", Id());
      start_read();
    }
  }

  /**
   * @brief Session 正常关闭的入口
   *
   * SslSession 会先和对方完成 TLS 挥手(close_notify)，然后再断开 socket；
   * NosslSession 会先发送 FIN，然后再断开 socket。
   *
   * 可以重复调用，只有第一次生效。
   *
   */
  void Shutdown() {
    if (!closing_.exchange(true)) {
      auto self(this->shared_from_this());
      asio::post(strand_, [self] { self->do_shutdown(); });
    }
  }

  /**
   * @brief Session 异常关闭的入口
   *
   * 不做 TLS 挥手，直接 cancel + 断开 socket；
   * 如果此时正常关闭正卡在等待对方的 close_notify，会打断这次等待。
   *
   * 可以重复调用，只有第一次生效。
   *
   */
  void Abort() {
    if (closed_.load()) return;
    aborted_.store(true);
    closing_.store(true);
    auto self(this->shared_from_this());
    asio::post(strand_, [self] { self->do_abort(); });
  }

  /**
   * @brief SessionHandle 接口的实现，向对端发送一条数据
   *
   * 线程安全，可以在任意线程调用；数据会按调用顺序排队，依次发送。
   * Session 关闭后调用不会发送任何数据。
   *
   * @param message 待发送的数据
   */
  void Send(std::string message) override {
    auto self(this->shared_from_this());
    asio::post(strand_, [self, message = std::move(message)]() mutable {
      self->do_send(std::move(message));
    });
  }

  /**
   * @brief SessionHandle 接口的实现，一次向对端发送多条数据
   *
   * 线程安全，可以在任意线程调用；这批数据会连续排队，按顺序发送，
   * 不会被别的线程发送的数据插到中间。Session 关闭后调用不会发送任何数据。
   *
   * @param messages 待发送的数据
   */
  void SendBatch(std::vector<std::string> messages) override {
    auto self(this->shared_from_this());
    asio::post(strand_, [self, messages = std::move(messages)]() mutable {
      self->do_send_batch(std::move(messages));
    });
  }

  /**
   * @brief SessionHandle 接口的实现，获取该 Session 的 id
   *
   * @return uint64_t
   */
  uint64_t Id() const override { return id_; }

 private:
  /**
   * @brief 仅当该 Session 为 SslSession 时调用
   *
   */
  void ssl_handshake() {
    static_assert(IsSslStream<Socket>, "Only SslSession can do ssl_handshake");

    auto self(this->shared_from_this());
    socket_.async_handshake(asio::ssl::stream_base::server,
                            [this, self](const std::error_code& error) {
                              if (!error) {
                                start_read();
                              } else {
                                handle_error(error);
                              }
                            });
  }

  /**
   * @brief 开始从对端读取消息
   *
   */
  void start_read() {
    auto valid_bufsize = streambuf_.size() + kChunkSize > streambuf_.max_size()
                             ? streambuf_.max_size() - streambuf_.size()
                             : kChunkSize;

    auto self(this->shared_from_this());
    socket_.async_read_some(
        streambuf_.prepare(valid_bufsize),
        asio::bind_executor(strand_,
                            [this, self](asio::error_code ec, size_t n) {
                              if (ec) {
                                handle_error(ec);
                              } else {
                                streambuf_.commit(n);
                                on_read_completed();
                                start_read();
                              }
                            }));
  }

  /**
   * @brief 一次读取完成之后的处理
   *
   * 取出 streambuf_ 中收到的 TLV 消息，交给 ServerContexts 处理；
   * 消息的解析由 ServerContexts 负责。
   *
   */
  void on_read_completed() {
    auto self(this->shared_from_this());
    for (;;) {
      if (closing_.load() || closed_.load()) return;

      // 检查 Tag 和 Length 字段
      if (streambuf_.size() < kHeaderSize) return;

      // 取出 Tag 和 Length 字段
      std::string header(kHeaderSize, '\0');
      asio::buffer_copy(asio::buffer(header), streambuf_.data(), kHeaderSize);
      auto tag = *reinterpret_cast<uint16_t*>(header.data());
      auto len = *reinterpret_cast<uint16_t*>(header.data() + kTagSize);
      if constexpr (std::endian::native != std::endian::big) {
        tag = std::byteswap(tag);
        len = std::byteswap(len);
      }

      // 检查 Value 字段
      std::size_t msg_len = kHeaderSize + len;
      if (streambuf_.size() < msg_len) return;

      // 取出 Value 字段
      std::string val(len, '\0');
      asio::buffer_copy(asio::buffer(val), streambuf_.data() + kHeaderSize,
                        len);

      // 消费缓冲区中相应的字符
      streambuf_.consume(msg_len);

      // 送出 tag 和 value 到 server_contexts_
      server_contexts_.OnSessionMessage(self, tag, std::move(val));
    }
  }

  /**
   * @brief 把数据放进发送队列，并尝试发送（只在 strand_ 中执行）
   *
   * @param message 待发送的数据
   */
  void do_send(std::string message) {
    assert(strand_.running_in_this_thread());

    // Session 正在关闭或者已经关闭，不再发送数据
    if (closing_.load() || closed_.load()) return;

    write_queue_.emplace_back(std::move(message));
    do_write_if_idle();
  }

  /**
   * @brief 把多条数据放进发送队列，并尝试发送（只在 strand_ 中执行）
   *
   * 整批数据连续入队，所以它们不会被别的数据插到中间；
   * 入队后才开始发送，避免出现"第一条已经发出、剩下的还在排队"。
   *
   * @param messages 待发送的数据
   */
  void do_send_batch(std::vector<std::string> messages) {
    assert(strand_.running_in_this_thread());

    // Session 正在关闭或者已经关闭，不再发送数据
    if (closing_.load() || closed_.load()) return;

    for (auto&& message : messages) {
      write_queue_.emplace_back(std::move(message));
    }
    do_write_if_idle();
  }

  /**
   * @brief 没有数据正在发送时，开始发送发送队列中的数据（只在 strand_ 中执行）
   *
   */
  void do_write_if_idle() {
    assert(strand_.running_in_this_thread());

    // 已经有数据在发送了，排队等待上一个发送完成
    if (writing_) return;
    do_write();
  }

  /**
   * @brief 发送发送队列中的数据（只在 strand_ 中执行）
   *
   * 一次发送的数据量不超过 kMaxBatchSize，队列中连续的小数据会被合并成
   * 一批一起发送，减少写次数；发送完成后继续发送下一批数据。
   *
   */
  void do_write() {
    assert(strand_.running_in_this_thread());
    if (write_queue_.empty()) return;

    writing_ = true;

    // 尽量从队列中取数据，只要加上下一条不超过 kMaxBatchSize 就继续取；
    // 至少取一条，避免单条数据超过 kMaxBatchSize 时一直发不出去
    auto batch = std::make_shared<WriteBatch>();
    std::size_t batch_size = 0;
    while (!write_queue_.empty()) {
      const std::string& next = write_queue_.front();
      if (!batch->messages.empty() &&
          batch_size + next.size() > kMaxBatchSize) {
        break;
      }
      batch_size += next.size();
      batch->messages.emplace_back(std::move(write_queue_.front()));
      write_queue_.pop_front();
    }

    // buffers 指向 messages 中的数据，必须在 messages 全部就位之后再构建
    batch->buffers.reserve(batch->messages.size());
    for (const std::string& message : batch->messages) {
      batch->buffers.emplace_back(asio::buffer(message));
    }

    auto self(this->shared_from_this());
    asio::async_write(
        socket_, batch->buffers,
        asio::bind_executor(
            strand_, [this, self, batch](std::error_code ec, std::size_t n) {
              writing_ = false;
              if (ec) {
                write_queue_.clear();
                handle_error(ec);
              } else {
                INFO("Session#{} write {} bytes in {} messages", Id(), n,
                     batch->messages.size());
                do_write();
              }
            }));
  }

  /**
   * @brief 错误处理的逻辑
   *
   */
  void handle_error(const std::error_code& error) {
    if (error == asio::error::eof) {
      // 对方先发起了正常的关闭(close_notify/FIN)，回一个正常的关闭
      INFO("Session#{} peer closed connection", Id());
      Shutdown();
    } else if (error == asio::error::operation_aborted) {
      // Session 正在关闭，读写被取消属于预期情况
    } else {
      ERROR("Session#{} error: {}", Id(), error.message());
      Abort();
    }
  }

  /**
   * @brief 处理正常关闭的逻辑（只在 strand_ 中执行）
   *
   */
  void do_shutdown() {
    assert(closing_.load() == true);
    assert(strand_.running_in_this_thread());

    // 正常关闭的过程中有异常关闭正在处理，交给正在处理的异常关闭
    if (aborted_.load() || closed_.load()) return;

    // NosslSession 已经可以正常关闭了
    if constexpr (!IsSslStream<Socket>) {
      close_socket(true);
    }

    // SslSession 需要先完成 TLS 挥手
    else {
      auto self(this->shared_from_this());

      // 注册 TLS 挥手超时定时器
      shutdown_timer_.expires_after(kShutdownTimeout);
      shutdown_timer_.async_wait(
          asio::bind_executor(strand_, [self](const std::error_code& error) {
            if (error) return;  // 挥手已经结束，定时器被取消
            WARN("Session#{} tls shutdown timeout", self->Id());
            self->do_abort();
          }));

      // 取消还在进行的读写，避免和 TLS 挥手重叠
      std::error_code _;
      socket_.lowest_layer().cancel(_);

      // 开始异步 TLS 挥手
      socket_.async_shutdown(
          asio::bind_executor(strand_, [self](const std::error_code& error) {
            self->shutdown_timer_.cancel();  // 挥手结束，取消挥手超时定时器
            if (error) {
              if (self->closed_.load()) return;  // 被异常关闭打断
              WARN("Session#{} tls shutdown failed: {}", self->Id(),
                   error.message());
              self->do_abort();  // 挥手失败，进行异常关闭
            } else {
              self->close_socket(true);  // 挥手成功
            }
          }));
    }
  }

  /**
   * @brief 处理异常关闭的逻辑（只在 strand_ 中执行）
   *
   */
  void do_abort() {
    assert(strand_.running_in_this_thread());
    close_socket(false);  // 直接断开 socket
  }

  /**
   * @brief 断开 socket（关闭 Session 的最后一步，只会断开一次）
   *
   * @param graceful true 表示正常关闭（TLS 挥手已完成 / FIN 已发送），
   *                 false 表示异常关闭，直接断开
   */
  void close_socket(bool graceful) {
    assert(strand_.running_in_this_thread());
    if (closed_.exchange(true)) return;

    std::error_code _;  // 忽略过程的所有错误
    shutdown_timer_.cancel();
    write_queue_.clear();  // 关闭后队列中的数据不再发送

    // 通知 ServerContexts 处理 Session 关闭相关的逻辑
    auto self(this->shared_from_this());
    server_contexts_.OnSessionClose(self);

    auto shutdown_type = graceful ? asio::socket_base::shutdown_send
                                  : asio::socket_base::shutdown_both;

    if constexpr (IsSslStream<Socket>) {
      socket_.lowest_layer().cancel(_);
      socket_.lowest_layer().shutdown(shutdown_type, _);
      socket_.lowest_layer().close(_);
    } else {
      socket_.cancel(_);
      socket_.shutdown(shutdown_type, _);
      socket_.close(_);
    }
  }

 private:
  /**
   * @brief 一批待发送的数据
   *
   * 异步发送完成之前，这批数据必须一直存活，所以用 shared_ptr 持有。
   *
   */
  struct WriteBatch {
    // 从发送队列中取出的数据
    std::vector<std::string> messages;
    // 指向 messages 中数据的 buffer 序列，用来一次发送整批数据
    std::vector<asio::const_buffer> buffers;
  };

 private:
  ServerContexts& server_contexts_;
  Socket socket_;

  // Session 的自增 id，创建时分配，用来区分不同的 Session
  const uint64_t id_{NextSessionId()};

  // 串行化该 Session 的读写操作
  asio::strand<asio::any_io_executor> strand_;

  // 读缓冲区
  asio::streambuf streambuf_;

  // 待发送数据的队列（只在 strand_ 中访问）
  std::deque<std::string> write_queue_;

  // 是否有数据正在发送中（只在 strand_ 中访问）
  bool writing_{false};

  // 当出现错误或者异常时，置位 closing_
  std::atomic_bool closing_{false};

  // 当出现错误需要直接断开 socket 时，置位 aborted_
  std::atomic_bool aborted_{false};

  // 当 socket 已经断开时，置位 closed_
  std::atomic_bool closed_{false};

  // 正常关闭时，等待 TLS 挥手完成的定时器
  asio::steady_timer shutdown_timer_;
};

using SslSession = Session<asio::ssl::stream<asio::ip::tcp::socket>>;
using NosslSession = Session<asio::ip::tcp::socket>;

#endif  // !BECHAT_CORE_SESSION_H_
