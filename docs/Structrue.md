# 处理 Session 收到的 TlvMessage：结构设计

> 本文档描述 BeChat v0.0.1 服务端**当前**的模块结构与设计决策，进度截至 Dispatcher 阶段。
> 与旧版不同，本文只描述代码里已经存在的东西；尚未实现的部分只列现状，后续蓝图见 `docs/Blueprint.md`。
>
> 配套文档：
>
> - `docs/DesignLine.md`：功能范围。
> - `docs/TlvProto.md`：线上字节格式、Tag、状态码。
> - `docs/Blueprint.md`：后续开发蓝图。
> - 本文档：服务端代码结构。

---

## 1. 文档目的与现状

本文档回答一个问题：

> `Session::read_value()` 读满一个 TLV 之后，消息如何走到处理、写响应、广播？

并说明：模块如何拆分、线程模型、一次请求的数据流，以及与旧版设计文档的差异。

当前进度总览：

**已实现**

- `TlvMessage` 大端序列化；`Session` 双 strand 读/写流水线。
- `ValueReader` / `ValueWriter` 字段级编解码。
- `TlvCodec`：`request_id` 提取与响应/错误/推送构造。
- `MessageTag`（含 `IsValidReqTag` / `ReqTag2Resp`）与 `StatusCode`。
- `Dispatcher`：Tag 校验 + `request_id` 提取 + 固定测试响应。
- `ServerContext` / `Acceptor` / `main` 接线。

**进行中 / 未实现**

- `Dispatcher` 尚未接入真正的业务 handler（当前返回固定测试响应）。
- `ServerContext::Broadcast` 是空实现（[TODO]）。
- `Session` 没有认证状态（未登录/已登录）和 `username_`。
- 没有业务状态：用户注册表、在线表、聊天室历史。
- 协议细节未落地：`max_payload` 校验、`request_id == 0` 校验、UTF-8 校验、各命令响应构造。

---

## 2. 总体结构

### 2.1 模块图

```text
main (bechat/bechat.cpp)
 ├─ IoThreads          // 1 个 io_context + N 线程   (utils/io_threads.{h,cpp})
 ├─ ServerContext      // 组合 Dispatcher，请求入口    (core/server_context.{h,cpp})
 └─ Acceptor           // 监听 + 建连                 (core/acceptor.{h,cpp})
      └─ Session       // 每个连接一条                (core/session.{h,cpp})
           └─ ServerContext::HandleRequest(SessionPtr, TlvMessagePtr)
                └─ Dispatcher::Dispatch(SessionPtr, TlvMessagePtr)   (proto/dispatcher.{h,cpp})
                     ├─ TlvCodec         // request_id、响应构造   (tlv/tlv_codec.{h,cpp})
                     ├─ MessageTag / StatusCode / DecodedRequest / RequestResult
                     └─ ValueReader / ValueWriter  // 字段级读写，header-only
```

### 2.2 依赖与所有权规则

- `main` 栈上依次创建 `IoThreads` → `ServerContext` → `Acceptor`；`Acceptor` 和 `Session` 都只持有 `ServerContext&` 引用，依赖 `main` 中的声明/析构顺序。
- 跨 strand、跨模块传递一律用 `shared_ptr`：`SessionPtr`、`TlvMessagePtr`（`utils/types.h`）。
- `Session` 只依赖 `ServerContext`，不包含 proto 头文件（`session.h` 只前置声明 `ServerContext`，`session.cpp` 才包含 `server_context.h`）。
- `ServerContext` 包含 `dispatcher.h`；`Dispatcher` 只依赖 TLV 层与 `proto/request.h`，不依赖 Session 实现。
- `TlvCodec` / `ValueReader` / `ValueWriter` 只依赖 `TlvMessage`、`MessageTag`、`StatusCode`、`utils/concepts.h`。

---

## 3. 线程模型

- `IoThreads(n)`：当前只创建 **1 个** `asio::io_context`，N 个线程同时 `run()` 这个 context（线程池）。不同 Session 的回调可能跑在多个线程上，单个 Session 内部靠 strand 串行。
- 每个 Session 两条 strand：
  - `read_strand_`：只跑读流程（`read_tag` → `read_length` → `read_value` → `on_read_completed`）。`input_msg_`、`msg_value_len_` 只在 read_strand_ 上访问。
  - `write_strand_`：跑「请求处理 + 写响应」（`on_read_completed` 投递的 lambda、`start_writing` 的回调、`handle_close` 的收尾）。`write_queue_`、`writing_` 只在 write_strand_ 上访问。
- `closing_` 是 `std::atomic_bool`，任意线程可读/置位。

关键约定：

- 读满一包后**拷贝** `input_msg_` 再 post 到 write_strand_，随后立即 `read_tag()` 读下一包——读不等处理，保持 TCP 全双工流水线。
- 当前没有共享业务状态（用户表/在线表/历史还没写），所以代码里还没有锁；这些状态加入后的锁策略见 `docs/Blueprint.md` §4。

---

## 4. 核心模块

### 4.1 TlvMessage — `tlv/tlv_message.{h,cpp}`

- 纯容器：`tag_` + `value_`（`ValueT = std::string`）。
- `length()` 直接返回 `value_.length()`，**没有独立的 length_ 字段**（与旧版设计不同，见 §6）。写侧无需同步字段；读侧长度暂存在 `Session::msg_value_len_`。
- `SerializeToString()`：reserve 后依次追加 `htobe16(tag)`、`htobe16(value_.size())`、value。

### 4.2 ValueReader / ValueWriter — `tlv/tlv_value_reader.h` / `tlv_value_writer.h`

- header-only，模板接口：`ReadInt<T>` / `WriteInt<T>`（`T` 满足 `Integer` 概念，`utils/concepts.h`），内部用 `std::byteswap` 做大小端转换。
- `ValueReader(std::string_view)`：`ReadString`（u16 长度 + 字节，检查越界）、`Remaining()`、`Done()`。
- `ValueWriter(std::string&)`：`WriteString`（assert 长度 ≤ UINT16_MAX）、`WriteBytes<span<T>>`（`T` 满足 `OneByte` 概念）。
- 两者只检查**线格式**，不做业务语义校验（长度限制、UTF-8 等属于业务层）。

### 4.3 TlvCodec — `tlv/tlv_codec.{h,cpp}`

| 接口                                               | 说明                                                                 |
| -------------------------------------------------- | -------------------------------------------------------------------- |
| `PeekRequestId(value) → std::optional<uint32_t>`   | Value ≥ 4 字节时返回前 4 字节大端 u32；否则 `nullopt`                |
| `PayloadAfterRequestId(value) → string_view`       | Value ≤ 4 字节时返回空视图；否则返回 `request_id` 之后的剩余部分     |
| `MakeResponse(resp_tag, request_id, status, body)` | 构造响应：request_id(u32) + status(u16) + body                       |
| `MakeError(request_id, request_tag, status)`       | 构造 `Resp::Error`：request_id(u32) + request_tag(u16) + status(u16) |
| `MakePush(push_tag, body)`                         | 构造推送：纯 body                                                    |

`PeekRequestId` 返回 `optional` 是相对旧版的简化：无法解析时由调用方（Dispatcher）填 `request_id = 0`。

### 4.4 MessageTag / StatusCode — `tlv/message_tag.h` / `tlv/status_code.h`

- `MessageTag::Req::Type` / `Resp::Type` / `Push::Type` 三组枚举，取值见 `TlvProto.md` §4。
- 相对旧版新增两个静态辅助函数，Dispatcher 靠它们做校验与映射：
  - `IsValidReqTag(Req::Type)`：已知请求 Tag 白名单（当前 5 个）。
  - `ReqTag2Resp(Req::Type)`：请求 Tag → 对应响应 Tag；未知返回 `Resp::Error`。
- `StatusCode::Type`：8 个状态码，见 `TlvProto.md` §5。

### 4.5 请求结果类型 — `proto/request.h`

```cpp
struct DecodedRequest {
  MessageTag::Req::Type tag;
  uint32_t request_id;
  std::string_view payload;  // 去掉 request_id 之后的剩余 value
};

struct RequestResult {
  std::vector<TlvMessagePtr> to_self_response;   // 只发给当前连接
  std::vector<TlvMessagePtr> to_broadcast_push;  // 广播给所有在线连接
};
```

- `DecodedRequest::payload` 是 `string_view`，指向投递到 write_strand_ 的那份 `TlvMessagePtr` 副本的 value；副本由投递 lambda 持有，生命周期覆盖同步处理。若未来 handler 异步化，payload 需改为 `std::string`。
- `RequestResult` 把「响应」和「推送」分开；处理顺序约定为「先 `to_self_response`，后 `to_broadcast_push`」（见 §4.8）。

### 4.6 Dispatcher — `proto/dispatcher.{h,cpp}`

当前是 `struct Dispatcher`，只有一个 `Dispatch` 方法，还没有 handler 注册表。逻辑：

1. `tag = static_cast<MessageTag::Req::Type>(request->tag())`；
2. `request_id = TlvCodec::PeekRequestId(request->value())`；
3. `!MessageTag::IsValidReqTag(tag) || !request_id` → 返回 `Resp::Error`（`MakeError(0, tag, MalformedPacket)`）；
4. 否则组装 `DecodedRequest`，当前返回固定测试响应 `MakeResponse(ReqTag2Resp(tag), request_id, Ok, "'OK message for test'")`——这一步是 [TODO]，接入业务 handler 的计划见 `docs/Blueprint.md` §3.4。

### 4.7 ServerContext — `core/server_context.{h,cpp}`

```cpp
class ServerContext {
 public:
  ServerContext(IoThreads& io_threads);

  RequestResult HandleRequest(SessionPtr session, TlvMessagePtr request);
  void Broadcast(SessionPtr session, TlvMessagePtr push);  // [TODO] 空实现

 private:
  IoThreads& io_threads_;
  Dispatcher dispatcher_;
};
```

- 构造接收 `IoThreads&`（`main` 先建 IoThreads 再建 ServerContext）。
- `HandleRequest` 直接转发给 `dispatcher_.Dispatch`。
- `Broadcast` 目前是空实现（[TODO]），目标语义见 `TlvProto.md` §6.6 / §6.7：把 Push 发给所有在线连接。

### 4.8 Session — `core/session.{h,cpp}`

传输壳，不包含任何业务。关键点：

**读循环**：`read_tag` → `read_length` → `read_value` 全部 `assert(read_strand_.running_in_this_thread())`；`read_value` 读满后调用 `on_read_completed()`，随后立即 `read_tag()` 读下一包。

**读 → 写交接**（`on_read_completed`，略去 assert 与日志）：

```cpp
void Session::on_read_completed() {
  auto self = shared_from_this();
  auto request_msg = std::make_shared<TlvMessage>(input_msg_);  // 必须拷贝
  asio::post(write_strand_, [this, self, request_msg]() {
    if (closing_) return;
    auto request_result = server_context_.HandleRequest(self, request_msg);
    if (closing_) return;
    write_queue_.push_range(request_result.to_self_response |
                            std::views::transform(&TlvMessage::SerializeToString));
    for (auto&& push : request_result.to_broadcast_push) {
      server_context_.Broadcast(self, std::move(push));
    }
    if (!writing_) start_writing();
  });
}
```

设计要点：

- **拷贝是必须的**：`input_msg_` 归 read_strand_，紧接着就被下一轮 `read_tag()` 复用；post 到 write_strand_ 的副本由 `request_msg` 持有，保证 `DecodedRequest::payload` 指向的内存有效。
- **投递顺序 == 读包顺序**：read_strand_ 按包到达顺序 post，write_strand_ 按 FIFO 执行，同一连接上处理顺序与接收顺序一致。
- **响应直接入队**：处理 lambda 已经在 write_strand_ 上，`to_self_response` 序列化后直接 push 进 `write_queue_`（`push_range` + transform，一次遍历），比「经 Send 再 post」少一次投递。
- **广播走 ServerContext**：`to_broadcast_push` 逐个交给 `ServerContext::Broadcast`（当前空实现）。
- `closing_` 在回调入口与处理返回后各检查一次；进入关闭流程后，排队中的包被丢弃。

**写**：`start_writing()` 从 `write_queue_` 取队首 `async_write`，写完回调 pop 后继续取下一包；出错走 `handle_error`。

**关闭**：`handle_error(ec)` 在 `closing_.exchange(true)` 幂等保护下调用 `handle_close()`；`handle_close` post 到 write_strand_，依次 `socket_.cancel / shutdown / close`（忽略 error_code）。当前 Session 没有认证状态，关闭时不清理任何业务数据。

### 4.9 Acceptor 与 main — `core/acceptor.{h,cpp}` / `bechat.cpp`

- `Acceptor` 持有 `ServerContext&`；`do_accept()` 中 `std::make_shared<Session>(std::move(socket), server_context_)->Start()`。
- `main`：`IoThreads io_threads(2);` → `ServerContext server_context(io_threads);` → `Acceptor acceptor(io_threads, "127.0.0.1", 35565, server_context);` → `io_threads.Run()`（阻塞至进程结束）。

### 4.10 工具与测试

- `utils/types.h`：`SessionPtr` / `TlvMessagePtr`。
- `utils/concepts.h`：`Integer` / `OneByte` 概念。
- `utils/io_threads.{h,cpp}`：`IoThreads`。
- `utils/logger.{h,cpp}`：spdlog 封装，`TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL` 宏。
- `example/tlv_sender.cpp`：交互式测试客户端（命令行逐条发 TLV 并打印响应）。
- `test/TestCMD.md`：nc 一行命令的手工测试。

---

## 5. 一次请求的完整数据流（当前）

```text
客户端字节
  → Session::read_tag / read_length / read_value          [read_strand_]
  → Session::on_read_completed                            [read_strand_]
      → 拷贝 input_msg_ → TlvMessagePtr
      → asio::post(write_strand_, 处理 lambda)
  → Session::read_tag  立即读下一包                        [read_strand_]
  → 处理 lambda                                           [write_strand_]
      → ServerContext::HandleRequest(self, request_msg)
          → Dispatcher::Dispatch
              → MessageTag::IsValidReqTag  校验 Tag
              → TlvCodec::PeekRequestId    提取 request_id
              → 非法：TlvCodec::MakeError → Resp::Error
              → 合法：组装 DecodedRequest，当前回固定测试响应
      → to_self_response 序列化入 write_queue_
      → to_broadcast_push → ServerContext::Broadcast（当前空实现）
      → if (!writing_) start_writing()                    [write_strand_]
```

---

## 6. 与旧版设计文档的差异

旧版 Structrue.md 是一份完整蓝图，实际实现做了这些偏离。记录在这里，避免文档与代码再次脱节：

| 旧版设计                                                                | 当前实现                                                                             |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| `TlvMessage` 保留 `length_` 字段，序列化时同步                          | 无 `length_`，`length()` 取 `value_.length()`；读侧用 `Session::msg_value_len_` 暂存 |
| `HandleRequest(Session&, const TlvMessage&)` 引用传递                   | `HandleRequest(SessionPtr, TlvMessagePtr)`，跨 strand 用 shared_ptr                  |
| `ServerContext` 默认构造、以 shared_ptr 传递                            | 构造接收 `IoThreads&`；`Acceptor` / `Session` 持有 `ServerContext&` 引用             |
| Dispatcher 用 raw_tag 区间检查 + `ToResponseTag(tag \| 0x8000)`         | `MessageTag::IsValidReqTag` 白名单 + `ReqTag2Resp` 映射                              |
| `PeekRequestId` 失败返回 0                                              | 返回 `std::optional<uint32_t>`，失败 `nullopt`，由 Dispatcher 填 0                   |
| Session 有 `SessionState` / `username_` / `Send` / `Shutdown`           | 尚未实现；目前只有 `handle_error` / `handle_close`                                   |
| `HandlerResult{to_self, to_broadcast}`，`TlvMessage` 值类型             | `RequestResult{to_self_response, to_broadcast_push}`，`TlvMessagePtr`                |
| TlvCodec 有 `MaxPayload` / `ToResponseTag` / `ToRawTag` / `IsValidUtf8` | 均未实现                                                                             |

---

## 7. 后续计划

从当前代码到 v0.0.1 完成的后续蓝图见 `docs/Blueprint.md`：新增模块（认证状态、业务状态容器、ChatService、Dispatcher 扩展）、关键决策与分阶段计划都在那里，本文不再重复。
