# 处理 Session 收到的 TlvMessage：详细结构设计

> 本文档是 v0.0.1 服务端结构设计，说明 `Session` 收到完整 `TlvMessage` 之后，如何从“读字节流”走到“业务处理、写响应、广播推送”。
>
> 当前代码已经具备：
>
> - `TlvMessage` 的 Tag / Length / Value 读写；
> - `Session` 的 read strand、write strand、`write_queue_`；
> - `MessageTag` 与 `StatusCode` 枚举。
>
> 当前代码还没有：
>
> - Value 编解码；
> - `request_id` 解析；
> - Dispatcher；
> - 用户、在线、历史等内存状态；
> - 真正的业务 handler。

---

## 1. 文档目的

本文档解决一个问题：

> `Session::read_value()` 读满一个 TLV 之后，应该发生什么？

并围绕这个问题，给出：

1. 模块如何拆分；
2. 每个模块的职责、接口和关键实现；
3. 模块之间的依赖和调用关系；
4. 线程模型和锁策略；
5. 请求处理流程；
6. 错误处理规则；
7. 实施顺序和测试方法。

本文档与以下文档配套：

- `docs/DesignLine.md`：功能范围。
- `docs/TlvProto.md`：线上字节格式、Tag、状态码。
- 本文档：服务端代码结构设计。

---

## 2. 设计目标与非目标

### 2.1 目标

1. **Session 只做传输**
   Session 负责字节流、TlvMessage 读写、响应写回和连接关闭，不直接理解注册/登录/聊天业务。

2. **业务逻辑集中**
   所有业务规则集中在 `ChatService`，由 `Dispatcher` 根据 Req Tag 路由。

3. **保持流水线能力**
   读完一个 TLV 后立即继续读下一个；响应通过 `request_id` 与请求对应。

4. **保持现有 strand 结构**
   继续使用 `read_strand_` 串行化每个连接的读流程，使用 `write_strand_` 串行化“请求处理 + 写”流程。

5. **共享状态线程安全**
   `UserRegistry`、`OnlineTable`、`HistoryStore` 会被多个 Session 的 write strand（业务 handler 所在 strand）同时访问，必须有统一锁策略。

6. **协议错误可恢复**
   TLV 边界是清楚的，单包解析失败时应回错误响应并继续读下一包，不随意关闭连接。

### 2.2 非目标

- 不引入数据库或持久化。
- 不实现 TLS。
- 不实现多聊天室、私聊。
- 不做请求并发数量限制。
- 不做服务端 `request_id` 去重或未完成请求表。
- 不引入全局 logic strand（当前先用 mutex；未来可以迁移）。

---

## 3. 现状与改造点

| 当前代码 | 问题 | 目标设计 |
|----------|------|----------|
| `Session::on_read_completed()` 把 `input_msg_` 拷贝后在 `write_strand_` 上回显 | 不理解请求语义 | 投递回调改为调用 `process_request()`，由 `ServerContext` 处理 |
| `Session` 没有认证状态 | 无法区分登录前/登录后 | 增加 `SessionState` 和 `username_` |
| 没有 Value 编解码 | 无法解析 `request_id`、String、u32/u64 | 新增 `TlvCodec` |
| 没有 Tag 分发 | 无法按 Req Tag 调用业务 | 新增 `Dispatcher` |
| 没有内存状态 | 无法注册/登录/聊天 | 新增 `UserRegistry`、`OnlineTable`、`HistoryStore` |
| `SerializeToString()` 使用可能过期的 `length_` | 调用方容易忘记同步 | 改为序列化时用 `value_.size()` 同步 `length_` |
| `Acceptor` 不持有业务上下文 | Session 无法访问共享状态 | `Acceptor` 持有 `shared_ptr<ServerContext>` 并传给 Session |

---

## 4. 总体架构

### 4.1 模块图

```text
┌──────────────────────────────────────────────────────────────────┐
│                              main                                 │
│  IoThreads + ServerContext + Acceptor                             │
└──────────────┬───────────────────────────────┬───────────────────┘
               │ shared_ptr<ServerContext>      │ accept
               ▼                               ▼
┌──────────────────────────────┐   ┌───────────────────────────────┐
│         Acceptor             │   │         IoThreads              │
│  持有 context_，创建 Session  │   │  一个 io_context + N 线程       │
└──────────────┬───────────────┘   └───────────────────────────────┘
               │ new Session(socket, context_)
               ▼
┌──────────────────────────────────────────────────────────────────┐
│                           Session                                 │
│                                                                   │
│  read_strand_：只读 TLV，读满一包后拷贝并 post 到 write_strand_，     │
│                随后立即 read_tag() 读下一包                         │
│  write_strand_：process_request 处理请求 + write_queue_ 串行写响应  │
│  state_ / username_：认证状态，只在 write_strand_ 上访问            │
│                                                                   │
│  void on_read_completed() {            // read_strand_            │
│    auto msg = make_shared<TlvMessage>(input_msg_);                │
│    post(write_strand_, [this, msg] {                              │
│      if (closing_) return;                                        │
│      process_request(*msg);                                       │
│      if (!writing_) start_writing();                              │
│    });                                                            │
│    read_tag();                                                    │
│  }                                                                │
│                                                                   │
│  void process_request(const TlvMessage& request) {                │
│    // 只在 write_strand_ 上调用                                    │
│    result = context_->HandleRequest(*this, request);              │
│    for (msg : result.to_self)                                     │
│      write_queue_.push(msg.SerializeToString());                  │
│    for (push : result.to_broadcast)                               │
│      context_->Broadcast(push);                                   │
│  }                                                                │
└──────────────┬───────────────────────────────────────────────────┘
               │ HandleRequest(session, request)
               ▼
┌──────────────────────────────────────────────────────────────────┐
│                       ServerContext                               │
│                                                                   │
│  ChatService chat_service_;   // 先构造                            │
│  Dispatcher dispatcher_;     // 后构造，引用 chat_service_          │
│                                                                   │
│  HandleRequest()  → dispatcher_.Dispatch(session, request)        │
│  Broadcast()      → chat_service_.Broadcast(push)                 │
│  OnSessionClosed()→ chat_service_.OnSessionClosed(username, this) │
└──────────────┬───────────────────────────────────────────────────┘
               │
               ▼
┌──────────────────────────────────────────────────────────────────┐
│                         Dispatcher                                │
│                                                                   │
│  raw_tag → 方向检查 → request_id 提取 → 已知/未知 Tag 判断          │
│  已知 Tag → ChatService::OnXxx(DecodedRequest, Session)           │
└──────────────┬───────────────────────────────────────────────────┘
               │
               ▼
┌──────────────────────────────────────────────────────────────────┐
│                        ChatService                                │
│                                                                   │
│  持有并保护：                                                      │
│    UserRegistry  users_;                                          │
│    OnlineTable   online_;                                         │
│    HistoryStore  history_;                                        │
│    std::mutex    state_mutex_;                                    │
│                                                                   │
│  OnRegister / OnLogin / OnSendMessage / OnGetHistory /            │
│  OnGetOnlineUsers / Broadcast / OnSessionClosed                   │
└──────────────┬───────────────────────────────────────────────────┘
               │
               ▼
┌──────────────────────────────────────────────────────────────────┐
│                          TlvCodec                                 │
│                                                                   │
│  ValueReader / ValueWriter                                        │
│  PeekRequestId / MakeResponse / MakeError / MakePush              │
│  ToResponseTag / IsValidUtf8                                      │
└──────────────────────────────────────────────────────────────────┘
```

### 4.2 依赖规则

```text
main
  └─ Acceptor
       ├─ IoThreads
       ├─ ServerContext
       └─ Session
            └─ ServerContext
                 ├─ Dispatcher
                 │    └─ ChatService
                 │         ├─ UserRegistry
                 │         ├─ OnlineTable
                 │         └─ HistoryStore
                 └─ TlvCodec
                      └─ TlvMessage
```

依赖规则：

1. `Session` 不包含任何 `service/*` 头文件，只依赖 `ServerContext`。
2. `ChatService` 头文件只前置声明 `Session`，在 `.cpp` 中再包含 `session.h`。
3. `Dispatcher` 不直接访问 `UserRegistry` 等状态，只调用 `ChatService`。
4. `TlvCodec` 只依赖 `TlvMessage`、`MessageTag`、`StatusCode`，不依赖 Session 或业务。
5. `OnlineTable` 不保存 `shared_ptr<Session>`，只保存 `weak_ptr<Session>`，避免循环所有权。

### 4.3 一次请求的完整数据流

```text
客户端发送字节
  → Session::read_tag()                         [read_strand_]
  → Session::read_length()                      [read_strand_]
  → Session::read_value()                       [read_strand_]
  → Session::on_read_completed()                [read_strand_]
      → 拷贝 input_msg_ → shared_ptr<TlvMessage>
      → asio::post(write_strand_, 处理回调)
  → Session::read_tag()                         [read_strand_，立即读下一包]
  → Session::process_request(*request)          [write_strand_]
      → ServerContext::HandleRequest()
          → Dispatcher::Dispatch()
              → TlvCodec::PeekRequestId()
              → ChatService::OnXxx()
                  → 读参数
                  → 检查 Session 状态
                  → 加 state_mutex_
                  → 读/写 UserRegistry / OnlineTable / HistoryStore
                  → 解锁
                  → 生成 HandlerResult
      → to_self 响应 SerializeToString 后直接 push 进 write_queue_
      → ServerContext::Broadcast(push)
          → ChatService::Broadcast()
              → 复制在线 Session 列表
              → 逐个 Session::Send(push)        [投递各 Session 的 write_strand_]
      → if (!writing_) start_writing()          [write_strand_]
```

关键点：

- 业务 handler 在 `write_strand_` 上同步执行，与写入共用同一条 strand，因此同一连接上“读取顺序 == 处理顺序 == 写入顺序”。
- 读满一个 TLV 后立刻开始读下一个 TLV，不等待处理与写完成，支持流水线。
- 本连接的响应在 `process_request` 中直接入 `write_queue_`；广播 Push 通过 `Send` post 回 `write_strand_`，入队晚于响应，因此对发送者自己而言响应先于 Push。

---

## 5. 建议目录与文件职责

```text
bechat/
  core/
    acceptor.h/.cpp
      - 监听 TCP，持有 shared_ptr<ServerContext>
      - 接受连接后创建 Session

    session.h/.cpp
      - TCP 传输壳
      - 读 TLV、把完整消息投递到 write_strand_、调用 ServerContext、写响应、关闭连接
      - 保存 SessionState 和 username

    server_context.h/.cpp
      - 服务端总入口
      - 持有 Dispatcher 和 ChatService
      - HandleRequest / Broadcast / OnSessionClosed

  tlv/
    tlv_message.h/.cpp
      - Tag / Length / Value 容器
      - SerializeToString

    tlv_codec.h/.cpp
      - ValueReader / ValueWriter
      - request_id 提取
      - MakeResponse / MakeError / MakePush
      - UTF-8 校验

    message_tag.h
      - MessageTag::Req / Resp / Push

    status_code.h
      - StatusCode::Type

  protocol/
    request.h
      - DecodedRequest
      - HandlerResult
      - SessionState（或放在 session.h，见下文）

    dispatcher.h/.cpp
      - Req Tag → Handler
      - 方向检查、request_id 检查、未知 Tag 处理

  service/
    user_registry.h/.cpp
      - 用户名密码注册表

    online_table.h/.cpp
      - 在线用户表
      - 重复登录顶替
      - 按 Session 身份移除

    history_store.h/.cpp
      - 单聊天室历史消息
      - seq 分配
      - 按 before_seq 翻页查询

    chat_service.h/.cpp
      - 所有业务 handler
      - 内部持有三个状态容器和一个 mutex
      - 构造响应和 Push
```

---

## 6. 线程模型与所有权

### 6.1 现有线程基础

当前 `IoThreads` 是一个 `asio::io_context` 加 N 个线程：

```cpp
IoThreads io_threads(2);
```

多个线程可能同时运行不同 Session 的回调，因此：

- 每个 Session 用 `read_strand_` 串行化自己的读流程。
- 每个 Session 用 `write_strand_` 串行化自己的“请求处理 + 写”流程。
- 不同 Session 之间可能并行。

### 6.2 业务在哪条 strand 上执行

v0.0.1 选择：

> **业务 handler 在 Session 的 `write_strand_` 上同步执行。**
>
> `read_strand_` 只负责读字节：读满一个完整 TLV 后，`on_read_completed()` 把消息拷贝出来，post 到 `write_strand_`，然后立刻 `read_tag()` 读下一包，不等处理完成。

原因：

1. **与现有代码结构一致**。当前 `on_read_completed()` 已经采用“拷贝 + post 到 `write_strand_`”的结构，只是把 TODO 回显换成真正的分发，strand 归属完全不变，演进成本最低。
2. **处理与写入同一条 strand，响应天然有序**。同一连接上消息按“读到的顺序”进入 `write_strand_` 的处理队列，响应也按同样的顺序入 `write_queue_`，因此流水线请求的“响应顺序 == 请求顺序”，不需要额外的响应重排。
3. **响应入队少一次跨 strand post**。`process_request` 本身就运行在 `write_strand_` 上，`to_self` 响应序列化后可以直接 `push` 进 `write_queue_`。
4. **Session 自身状态无需额外同步**。`state_` / `username_` 只被 `write_strand_` 访问，业务 handler 在 `write_strand_` 上直接读写它们，不需要锁。
5. **读侧完全不受业务影响**。读满一包后立即继续读下一包，读永远领先于处理，保持 TCP 全双工流水线；业务再慢也只是推迟处理与写，不会阻塞读。

代价：

- 处理与写共用同一条 strand，本连接的慢 handler 会推迟本连接后续请求的处理和所有写入（但不阻塞读）。
- 当前不存在真正的慢操作（无磁盘、无网络调用），可接受。

澄清一个常见误解：

- `write_strand_` 只串行“投递到它上面的 handler”，并不会等待挂在 OS 层的未完成异步操作。也就是说 `async_write` 在途期间，strand 仍然会继续执行后面排队的 `process_request`，慢客户端不会卡住后续消息的处理；写队列只会变长。

未来优化：

- 如果业务变重，可把 `ChatService` 迁移到全局 `asio::strand`（logic strand）；
- Session 在 `on_read_completed` 中只提交 `DecodedRequest`，响应再 post 回 Session 的 `write_strand_`。

### 6.3 共享状态锁策略

`ChatService` 内部用一个 `std::mutex state_mutex_` 保护：

- `UserRegistry`
- `OnlineTable`
- `HistoryStore`

锁规则：

1. 只在内存读写时持锁。
2. 持锁期间不得执行 socket I/O。
3. 持锁期间不得调用 `Session::Send` / `Session::Shutdown` 之外可能回锁业务层的函数。
4. 需要关闭旧 Session 时，先复制 `shared_ptr<Session>`，解锁后再调用 `Shutdown()`。
5. Push 广播时，先在锁内复制 `weak_ptr<Session>` 列表，解锁后再逐个 `Send`。

### 6.4 Session 字段访问规则

| 字段 | 访问 strand | 说明 |
|------|-------------|------|
| `socket_` | 读写都可能，但通过 strand 串行 | socket 操作要 post 到对应 strand |
| `closing_` | 任意线程 | `std::atomic_bool` |
| `input_msg_` | 只在 `read_strand_` | 读取缓冲区；读满一包后马上被 `read_tag()` 复用，投递前必须拷贝 |
| `state_` | 只在 `write_strand_` | 认证状态；业务 handler 与 `process_request` 都运行在 `write_strand_` 上，直接读写 |
| `username_` | 只在 `write_strand_` | 登录用户名；同上 |
| `write_queue_` | 只在 `write_strand_` | 待写响应 |
| `writing_` | 只在 `write_strand_` | 是否正在写 |

### 6.5 所有权与生命周期

- `main` 创建 `shared_ptr<ServerContext>`。
- `Acceptor` 持有 `shared_ptr<ServerContext>`。
- 每个 `Session` 持有 `shared_ptr<ServerContext>`。
- 异步读写回调都持有 `shared_ptr<Session>`（`self`）。
- `OnlineTable` 只保存 `weak_ptr<Session>`。
- 因此不会出现 `OnlineTable → Session → ServerContext → ChatService → OnlineTable` 的强引用环。

---

## 7. 核心数据结构

### 7.1 TlvMessage 的调整

当前 `TlvMessage` 已经足够承担“容器”职责，但建议把序列化改成：

```cpp
std::string TlvMessage::SerializeToString() {
  // 序列化时以 value_ 的实际大小为准，避免调用方忘记同步 length_
  assert(value_.size() <= std::numeric_limits<LengthT>::max());
  length_ = static_cast<LengthT>(value_.size());

  std::string result;
  result.reserve(sizeof(TagT) + sizeof(LengthT) + value_.size());

  const auto tag = htobe16(tag_);
  result.append(reinterpret_cast<const char*>(&tag), sizeof(tag));

  const auto length = htobe16(length_);
  result.append(reinterpret_cast<const char*>(&length), sizeof(length));

  result.append(value_);
  return result;
}
```

设计意图：

- 响应构造时只关心 `set_tag()` 和 `set_value()`，不用手工 `set_length()`。
- 读取路径仍然使用 `mutable_length()` 读入 `length_`，再按 `length_` 分配 Value。
- 如果 `value_.size() > 65535`，上层必须提前拒绝；当前协议限制保证这一点。

### 7.2 SessionState

建议放在 `session.h`，因为它是 Session 自身的状态：

```cpp
enum class SessionState {
  Unauthenticated,
  Authenticated,
};
```

### 7.3 DecodedRequest

```cpp
struct DecodedRequest {
  MessageTag::Req::Type tag;
  uint32_t request_id;
  std::string_view payload;  // 去掉 request_id 之后的剩余 Value
};
```

设计意图：

- `request_id` 由 Dispatcher 统一提取，handler 不需要重复解析。
- `payload` 是 `std::string_view`，指向投递时拷贝出来的 `shared_ptr<TlvMessage>` 的 Value；该副本由投递到 `write_strand_` 的 lambda 持有，而 handler 在同一 lambda 内同步执行，因此生命周期覆盖 handler。
- 如果未来 handler 异步化（或迁移到全局 logic strand），必须把 `payload` 复制为 `std::string`。

### 7.4 HandlerResult

```cpp
struct HandlerResult {
  std::vector<TlvMessage> to_self;       // 只发给当前连接，按数组顺序写
  std::vector<TlvMessage> to_broadcast;  // 广播给所有在线连接

  static HandlerResult Response(TlvMessage message) {
    HandlerResult result;
    result.to_self.push_back(std::move(message));
    return result;
  }

  static HandlerResult Broadcast(TlvMessage message) {
    HandlerResult result;
    result.to_broadcast.push_back(std::move(message));
    return result;
  }
};
```

设计意图：

- 把“响应”和“推送”分开。
- `Session` 先处理 `to_self`，再处理 `to_broadcast`，从而满足“登录响应先于 UserJoinedPush”“发送响应先于 NewMessagePush”的协议顺序。
- 用 vector 而不是单个对象，是为了未来一个请求产生多个响应或多个 Push。

### 7.5 OnlineUser / ChatRecord

```cpp
struct OnlineUser {
  std::string username;
  uint64_t login_timestamp;  // Unix 毫秒
};

struct ChatRecord {
  uint64_t seq;
  std::string sender;
  uint64_t timestamp;  // Unix 毫秒
  std::string content;
};
```

---

## 8. TlvCodec 详细设计

### 8.1 职责

`TlvCodec` 只处理“字段级”编解码，不理解注册/登录等业务：

```cpp
class TlvCodec {
 public:
  // v0.0.1 固定为 65535；后续可改为从配置读取
  static constexpr uint16_t MaxPayload = 65535;

  static uint32_t PeekRequestId(const std::string& value);
  static std::string_view PayloadAfterRequestId(const std::string& value);

  static TlvMessage MakeResponse(MessageTag::Resp::Type resp_tag,
                                 uint32_t request_id,
                                 StatusCode::Type status,
                                 std::string_view body = {});

  static TlvMessage MakeError(uint32_t request_id,
                              uint16_t request_tag,
                              StatusCode::Type status);

  static TlvMessage MakePush(MessageTag::Push::Type push_tag,
                             std::string_view body = {});

  static uint16_t ToResponseTag(uint16_t request_tag);
  static uint16_t ToRawTag(MessageTag::Req::Type request_tag);

  static bool IsValidUtf8(std::string_view text);
};
```

### 8.2 ValueReader

```cpp
class ValueReader {
 public:
  explicit ValueReader(std::string_view data) : data_(data) {}

  bool ReadU16(uint16_t& out) {
    if (!Ensure(sizeof(uint16_t))) return false;
    out = ReadU16BE(data_.substr(pos_, sizeof(uint16_t)));
    pos_ += sizeof(uint16_t);
    return true;
  }

  bool ReadU32(uint32_t& out) {
    if (!Ensure(sizeof(uint32_t))) return false;
    out = ReadU32BE(data_.substr(pos_, sizeof(uint32_t)));
    pos_ += sizeof(uint32_t);
    return true;
  }

  bool ReadU64(uint64_t& out) {
    if (!Ensure(sizeof(uint64_t))) return false;
    out = ReadU64BE(data_.substr(pos_, sizeof(uint64_t)));
    pos_ += sizeof(uint64_t);
    return true;
  }

  // 协议 String：u16 length + bytes
  // 这里只检查线格式，不检查业务长度和 UTF-8
  bool ReadString(std::string& out) {
    uint16_t len = 0;
    if (!ReadU16(len)) return false;
    if (len > Remaining()) return false;

    out.assign(data_.substr(pos_, len));
    pos_ += len;
    return true;
  }

  bool Done() const { return pos_ == data_.size(); }
  size_t Remaining() const { return data_.size() - pos_; }

 private:
  bool Ensure(size_t n) const { return n <= Remaining(); }

  std::string_view data_;
  size_t pos_{0};
};
```

### 8.3 ValueWriter

```cpp
class ValueWriter {
 public:
  explicit ValueWriter(std::string& out) : out_(out) {}

  void WriteU16(uint16_t value) {
    WriteU16BE(out_, value);
  }

  void WriteU32(uint32_t value) {
    WriteU32BE(out_, value);
  }

  void WriteU64(uint64_t value) {
    WriteU64BE(out_, value);
  }

  void WriteString(std::string_view value) {
    assert(value.size() <= std::numeric_limits<uint16_t>::max());
    WriteU16(static_cast<uint16_t>(value.size()));
    WriteBytes(value);
  }

  void WriteBytes(std::string_view bytes) {
    out_.append(bytes.data(), bytes.size());
  }

 private:
  std::string& out_;
};
```

### 8.4 字节序工具

```cpp
uint16_t ReadU16BE(std::string_view data) {
  uint16_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return be16toh(value);
}

uint32_t ReadU32BE(std::string_view data) {
  uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return be32toh(value);
}

uint64_t ReadU64BE(std::string_view data) {
  uint64_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return be64toh(value);
}

void WriteU16BE(std::string& out, uint16_t value) {
  const auto encoded = htobe16(value);
  out.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

void WriteU32BE(std::string& out, uint32_t value) {
  const auto encoded = htobe32(value);
  out.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

void WriteU64BE(std::string& out, uint64_t value) {
  const auto encoded = htobe64(value);
  out.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}
```

### 8.5 request_id 提取

```cpp
uint32_t TlvCodec::PeekRequestId(const std::string& value) {
  if (value.size() < sizeof(uint32_t)) {
    return 0;
  }
  return ReadU32BE(std::string_view(value).substr(0, sizeof(uint32_t)));
}

std::string_view TlvCodec::PayloadAfterRequestId(const std::string& value) {
  if (value.size() < sizeof(uint32_t)) {
    return {};
  }
  return std::string_view(value).substr(sizeof(uint32_t));
}
```

### 8.6 响应 / 错误 / Push 构造

```cpp
TlvMessage TlvCodec::MakeResponse(MessageTag::Resp::Type resp_tag,
                                  uint32_t request_id,
                                  StatusCode::Type status,
                                  std::string_view body) {
  TlvMessage response;
  response.set_tag(static_cast<uint16_t>(resp_tag));

  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + body.size());

  ValueWriter writer(value);
  writer.WriteU32(request_id);
  writer.WriteU16(static_cast<uint16_t>(status));
  writer.WriteBytes(body);

  response.set_value(value);
  return response;  // SerializeToString 会同步 length_
}

TlvMessage TlvCodec::MakeError(uint32_t request_id,
                               uint16_t request_tag,
                               StatusCode::Type status) {
  TlvMessage error;
  error.set_tag(static_cast<uint16_t>(MessageTag::Resp::Error));

  std::string value;
  value.reserve(sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t));

  ValueWriter writer(value);
  writer.WriteU32(request_id);
  writer.WriteU16(request_tag);
  writer.WriteU16(static_cast<uint16_t>(status));

  error.set_value(value);
  return error;
}

TlvMessage TlvCodec::MakePush(MessageTag::Push::Type push_tag,
                              std::string_view body) {
  TlvMessage push;
  push.set_tag(static_cast<uint16_t>(push_tag));
  push.set_value(std::string(body));
  return push;
}

uint16_t TlvCodec::ToResponseTag(uint16_t request_tag) {
  return static_cast<uint16_t>(request_tag | 0x8000);
}

uint16_t TlvCodec::ToRawTag(MessageTag::Req::Type request_tag) {
  return static_cast<uint16_t>(request_tag);
}
```

### 8.7 UTF-8 校验

```cpp
bool TlvCodec::IsValidUtf8(std::string_view text) {
  size_t i = 0;
  while (i < text.size()) {
    const uint8_t c = static_cast<uint8_t>(text[i]);

    if (c <= 0x7F) {
      i += 1;
      continue;
    }

    size_t extra = 0;
    uint32_t codepoint = 0;

    if ((c & 0xE0) == 0xC0) {
      extra = 1;
      codepoint = c & 0x1F;
      if (codepoint < 2) return false;  // 拒绝过长编码
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      codepoint = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      codepoint = c & 0x07;
    } else {
      return false;
    }

    if (i + extra >= text.size()) return false;

    for (size_t j = 1; j <= extra; ++j) {
      const uint8_t next = static_cast<uint8_t>(text[i + j]);
      if ((next & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3F);
    }

    if ((extra == 2 && codepoint < 0x800) ||
        (extra == 3 && codepoint < 0x10000) ||
        codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
      return false;
    }

    i += extra + 1;
  }
  return true;
}
```

---

## 9. Dispatcher 详细设计

### 9.1 职责

1. 检查客户端发送的 Tag 是否在 `MessageTag::Req` 区间。
2. 提取 `request_id`。
3. 未知 Tag、方向错误、长度不足时构造 `Resp::Error`。
4. 已知 Tag 时构造 `DecodedRequest` 并调用对应 handler。
5. 捕获 handler 异常，避免单个请求错误导致 Session 崩溃。

### 9.2 头文件

```cpp
class Session;

class Dispatcher {
 public:
  using Handler =
      std::function<HandlerResult(const DecodedRequest&, Session&)>;

  explicit Dispatcher(ChatService& chat_service);

  HandlerResult Dispatch(Session& session, const TlvMessage& request) const;

 private:
  std::unordered_map<MessageTag::Req::Type, Handler> handlers_;
};
```

### 9.3 注册 handler

```cpp
Dispatcher::Dispatcher(ChatService& chat_service) {
  handlers_.emplace(
      MessageTag::Req::Register,
      [&chat_service](const DecodedRequest& request, Session& session) {
        return chat_service.OnRegister(request, session);
      });

  handlers_.emplace(
      MessageTag::Req::Login,
      [&chat_service](const DecodedRequest& request, Session& session) {
        return chat_service.OnLogin(request, session);
      });

  handlers_.emplace(
      MessageTag::Req::SendMessage,
      [&chat_service](const DecodedRequest& request, Session& session) {
        return chat_service.OnSendMessage(request, session);
      });

  handlers_.emplace(
      MessageTag::Req::GetHistory,
      [&chat_service](const DecodedRequest& request, Session& session) {
        return chat_service.OnGetHistory(request, session);
      });

  handlers_.emplace(
      MessageTag::Req::GetOnlineUsers,
      [&chat_service](const DecodedRequest& request, Session& session) {
        return chat_service.OnGetOnlineUsers(request, session);
      });
}
```

### 9.4 Dispatch 实现

```cpp
HandlerResult Dispatcher::Dispatch(Session& session,
                                   const TlvMessage& request) const {
  const uint16_t raw_tag = request.tag();

  // 客户端只能发送 0x0001 ~ 0x00FF
  if (raw_tag < static_cast<uint16_t>(MessageTag::Req::Register) ||
      raw_tag > 0x00FF) {
    return HandlerResult::Response(TlvCodec::MakeError(0, raw_tag,
                                StatusCode::MalformedPacket));
  }

  const std::string& value = request.value();

  // 所有 Req 请求至少要有 4 字节 request_id
  if (value.size() < sizeof(uint32_t)) {
    return HandlerResult::Response(TlvCodec::MakeError(0, raw_tag,
                                StatusCode::MalformedPacket));
  }

  const uint32_t request_id = TlvCodec::PeekRequestId(value);
  const auto request_type =
      static_cast<MessageTag::Req::Type>(raw_tag);

  const auto handler_it = handlers_.find(request_type);
  if (handler_it == handlers_.end()) {
    // 未知但仍在 Req 区间：回传前 4 字节 request_id
    return HandlerResult::Response(TlvCodec::MakeError(request_id, raw_tag,
                                StatusCode::MalformedPacket));
  }

  // request_id 0 保留给无法解析的情况，客户端不得使用
  if (request_id == 0) {
    return HandlerResult::Response(TlvCodec::MakeResponse(
        static_cast<MessageTag::Resp::Type>(
            TlvCodec::ToResponseTag(raw_tag)),
        0,
        StatusCode::InvalidParameter));
  }

  DecodedRequest decoded_request{
      request_type,
      request_id,
      TlvCodec::PayloadAfterRequestId(value),
  };

  try {
    return handler_it->second(decoded_request, session);
  } catch (const std::exception& e) {
    ERROR("Handler exception for tag 0x{:x}: {}", raw_tag, e.what());
    return HandlerResult::Response(TlvCodec::MakeResponse(
        static_cast<MessageTag::Resp::Type>(
            TlvCodec::ToResponseTag(raw_tag)),
        request_id,
        StatusCode::InternalError));
  } catch (...) {
    ERROR("Unknown handler exception for tag 0x{:x}", raw_tag);
    return HandlerResult::Response(TlvCodec::MakeResponse(
        static_cast<MessageTag::Resp::Type>(
            TlvCodec::ToResponseTag(raw_tag)),
        request_id,
        StatusCode::InternalError));
  }
}
```

### 9.5 设计意图

- 方向、长度、未知 Tag 在 Dispatcher 统一处理，业务 handler 只处理“格式正确的已知请求”。
- `request_id == 0` 是参数错误，走对应响应 Tag，而不是 `Resp::Error`。
- handler 抛异常时返回 `StatusCode::InternalError`，保持连接可用。

---

## 10. Service 层详细设计

### 10.1 UserRegistry

```cpp
class UserRegistry {
 public:
  // 调用方必须已经持有 ChatService::state_mutex_
  bool Register(const std::string& username, const std::string& password) {
    if (users_.contains(username)) {
      return false;
    }
    users_.emplace(username, password);
    return true;
  }

  // 调用方必须已经持有 ChatService::state_mutex_
  bool Verify(const std::string& username, const std::string& password) const {
    const auto it = users_.find(username);
    return it != users_.end() && it->second == password;
  }

 private:
  std::unordered_map<std::string, std::string> users_;
};
```

设计意图：

- 当前 v0.0.1 按 `TlvProto.md`，密码明文传输并内存保存。
- `UserRegistry` 自身不加锁，统一由 `ChatService::state_mutex_` 保护。
- 不把“用户存在”和“密码正确”拆成两个接口，登录只调用 `Verify`。

### 10.2 OnlineTable

```cpp
class OnlineTable {
 public:
  // 说明：为了减少头文件依赖，下面的实现建议放到 online_table.cpp，
  // 头文件只保留声明；这里写在一起是为了展示完整逻辑。
  // 以下方法都要求调用方已经持有 ChatService::state_mutex_

  // 添加或替换在线 Session；返回被替换的旧 Session
  std::shared_ptr<Session> Put(const std::string& username,
                               std::weak_ptr<Session> session,
                               uint64_t login_timestamp) {
    std::shared_ptr<Session> old_session;

    if (const auto it = entries_.find(username); it != entries_.end()) {
      old_session = it->second.session.lock();
    }

    entries_[username] = Entry{std::move(session), login_timestamp};
    return old_session;
  }

  // 只有当前表项确实指向 expected_session 时才移除
  bool RemoveIdentity(const std::string& username, Session* expected_session) {
    const auto it = entries_.find(username);
    if (it == entries_.end()) {
      return false;
    }

    const auto current = it->second.session.lock();
    if (!current) {
      entries_.erase(it);  // Session 已析构，清理残留条目
      return false;
    }

    if (current.get() != expected_session) {
      return false;  // 已经被新连接顶替
    }

    entries_.erase(it);
    return true;
  }

  std::vector<OnlineUser> Snapshot() const {
    std::vector<OnlineUser> users;
    users.reserve(entries_.size());

    for (const auto& [username, entry] : entries_) {
      if (entry.session.expired()) {
        continue;  // 防御：理论上关闭流程会主动移除
      }
      users.push_back(OnlineUser{username, entry.login_timestamp});
    }

    std::sort(users.begin(), users.end(),
              [](const OnlineUser& lhs, const OnlineUser& rhs) {
                return lhs.login_timestamp < rhs.login_timestamp;
              });
    return users;
  }

  std::vector<std::shared_ptr<Session>> Sessions() const {
    std::vector<std::shared_ptr<Session>> sessions;
    sessions.reserve(entries_.size());

    for (const auto& [username, entry] : entries_) {
      if (auto session = entry.session.lock()) {
        sessions.push_back(std::move(session));
      }
    }
    return sessions;
  }

 private:
  struct Entry {
    std::weak_ptr<Session> session;
    uint64_t login_timestamp;
  };

  std::unordered_map<std::string, Entry> entries_;
};
```

设计意图：

- 只保存 `weak_ptr<Session>`，OnlineTable 不会延长 Session 生命周期。
- `Put` 返回旧 Session，让调用方在解锁后执行 `old->Shutdown()`。
- `RemoveIdentity` 比较 Session 指针，防止旧连接关闭时误删新连接。
- `Sessions()` 返回 `shared_ptr` 副本，Broadcast 在解锁后逐个发送。

### 10.3 HistoryStore

```cpp
class HistoryStore {
 public:
  // 调用方必须已经持有 ChatService::state_mutex_
  ChatRecord Append(const std::string& sender,
                    const std::string& content,
                    uint64_t timestamp) {
    ChatRecord record{
        next_seq_++,
        sender,
        timestamp,
        content,
    };
    records_.push_back(record);
    return record;
  }

  // 调用方必须已经持有 ChatService::state_mutex_
  // 返回 seq < before_seq 的最新 limit 条，按 seq 升序。
  // before_seq == 0 表示从最新一条开始取。
  std::vector<ChatRecord> Get(uint64_t before_seq, uint16_t limit) const {
    if (limit == 0) {
      return {};
    }

    std::vector<ChatRecord> selected;
    selected.reserve(limit);

    for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
      if (before_seq == 0 || it->seq < before_seq) {
        selected.push_back(*it);
        if (selected.size() == limit) {
          break;
        }
      }
    }

    std::reverse(selected.begin(), selected.end());
    return selected;
  }

 private:
  std::deque<ChatRecord> records_;
  uint64_t next_seq_{1};
};
```

设计意图：

- 单聊天室，所以历史不需要 room 维度。
- 从后往前选最新 N 条，再反转成升序。
- v0.0.1 不设历史上限；未来只需在 `Append` 中增加淘汰逻辑。

### 10.4 ChatService

#### 10.4.1 头文件

`chat_service.h` 需要包含 `tlv_codec.h`（因为 `MaxPayload` 引用了 `TlvCodec::MaxPayload`）和 `request.h`。

```cpp
class Session;

class ChatService {
 public:
  HandlerResult OnRegister(const DecodedRequest& request, Session& session);
  HandlerResult OnLogin(const DecodedRequest& request, Session& session);
  HandlerResult OnSendMessage(const DecodedRequest& request, Session& session);
  HandlerResult OnGetHistory(const DecodedRequest& request, Session& session);
  HandlerResult OnGetOnlineUsers(const DecodedRequest& request,
                                 Session& session);

  void Broadcast(TlvMessage message);
  void OnSessionClosed(const std::string& username, Session* session);

 private:
  TlvMessage MakeSimpleResponse(MessageTag::Resp::Type tag,
                                uint32_t request_id,
                                StatusCode::Type status) const;

  TlvMessage BuildUserJoinedPush(const std::string& username,
                                 uint64_t login_timestamp) const;
  TlvMessage BuildSendMessageResponse(uint32_t request_id,
                                      const ChatRecord& record) const;
  TlvMessage BuildNewMessagePush(const ChatRecord& record) const;
  TlvMessage BuildHistoryResponse(
      uint32_t request_id,
      const std::vector<ChatRecord>& records) const;
  TlvMessage BuildOnlineUsersResponse(
      uint32_t request_id,
      const std::vector<OnlineUser>& users) const;

  static uint64_t NowUnixMs();

  // 与 TlvCodec::MaxPayload 保持一致；v0.0.1 固定为 65535
  static constexpr uint16_t MaxPayload = TlvCodec::MaxPayload;
  static constexpr uint16_t DefaultHistoryLimit = 50;
  static constexpr uint16_t MaxHistoryLimit = 200;
  static constexpr size_t MaxUsernameBytes = 32;
  static constexpr size_t MinPasswordBytes = 6;
  static constexpr size_t MaxPasswordBytes = 64;
  static constexpr size_t MaxContentBytes = 65475;

  std::mutex state_mutex_;
  UserRegistry users_;
  OnlineTable online_;
  HistoryStore history_;
};
```

时间戳实现：

```cpp
uint64_t ChatService::NowUnixMs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count());
}
```

#### 10.4.2 OnRegister

```cpp
HandlerResult ChatService::OnRegister(const DecodedRequest& request,
                                      Session& session) {
  // 1. 先解析参数：ValueReader 只检查线格式。
  //    按 TlvProto 的错误处理顺序，解析失败优先于状态和业务检查。
  ValueReader reader(request.payload);
  std::string username;
  std::string password;

  if (!reader.ReadString(username) ||
      !reader.ReadString(password) ||
      !reader.Done()) {
    return HandlerResult::Response(TlvCodec::MakeError(request.request_id,
                                TlvCodec::ToRawTag(request.tag),
                                StatusCode::MalformedPacket));
  }

  // 2. 状态检查
  if (session.state() == SessionState::Authenticated) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::Register,
                               request.request_id,
                               StatusCode::InvalidState));
  }

  // 3. 语义校验
  if (username.empty() || username.size() > MaxUsernameBytes ||
      password.size() < MinPasswordBytes ||
      password.size() > MaxPasswordBytes ||
      !TlvCodec::IsValidUtf8(username) ||
      !TlvCodec::IsValidUtf8(password)) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::Register,
                               request.request_id,
                               StatusCode::InvalidParameter));
  }

  // 4. 业务状态
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!users_.Register(username, password)) {
      return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::Register,
                                 request.request_id,
                                 StatusCode::UsernameAlreadyExists));
    }
  }

  return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::Register,
                             request.request_id,
                             StatusCode::Ok));
}
```

#### 10.4.3 OnLogin

```cpp
HandlerResult ChatService::OnLogin(const DecodedRequest& request,
                                   Session& session) {
  // 1. 先解析线格式
  ValueReader reader(request.payload);
  std::string username;
  std::string password;

  if (!reader.ReadString(username) ||
      !reader.ReadString(password) ||
      !reader.Done()) {
    return HandlerResult::Response(TlvCodec::MakeError(request.request_id,
                                TlvCodec::ToRawTag(request.tag),
                                StatusCode::MalformedPacket));
  }

  // 2. 状态检查
  if (session.state() == SessionState::Authenticated) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::Login,
                               request.request_id,
                               StatusCode::InvalidState));
  }

  // 3. 语义校验
  if (username.empty() || username.size() > MaxUsernameBytes ||
      password.size() < MinPasswordBytes ||
      password.size() > MaxPasswordBytes ||
      !TlvCodec::IsValidUtf8(username) ||
      !TlvCodec::IsValidUtf8(password)) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::Login,
                               request.request_id,
                               StatusCode::InvalidParameter));
  }

  std::shared_ptr<Session> old_session;
  uint64_t login_timestamp = 0;

  {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (!users_.Verify(username, password)) {
      return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::Login,
                                 request.request_id,
                                 StatusCode::InvalidCredentials));
    }

    login_timestamp = NowUnixMs();
    old_session = online_.Put(username, session.weak_from_this(),
                              login_timestamp);
  }

  // 当前 handler 运行在 session 的 write_strand_ 上，可以安全设置状态（见 6.4 / 11.2）
  session.SetAuthenticated(username);

  // 顶替旧连接；必须在解锁后调用
  if (old_session && old_session.get() != &session) {
    old_session->Shutdown();
  }

  HandlerResult result;
  result.to_self.push_back(MakeSimpleResponse(
      MessageTag::Resp::Login, request.request_id, StatusCode::Ok));
  result.to_broadcast.push_back(
      BuildUserJoinedPush(username, login_timestamp));
  return result;
}
```

#### 10.4.4 OnSendMessage

```cpp
HandlerResult ChatService::OnSendMessage(const DecodedRequest& request,
                                         Session& session) {
  // 1. 先解析线格式
  ValueReader reader(request.payload);
  std::string content;

  if (!reader.ReadString(content) || !reader.Done()) {
    return HandlerResult::Response(TlvCodec::MakeError(request.request_id,
                                TlvCodec::ToRawTag(request.tag),
                                StatusCode::MalformedPacket));
  }

  // 2. 登录检查
  if (session.state() != SessionState::Authenticated) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::SendMessage,
                               request.request_id,
                               StatusCode::NotLoggedIn));
  }

  // 3. 语义校验
  if (content.empty() ||
      content.size() > MaxContentBytes ||
      !TlvCodec::IsValidUtf8(content)) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::SendMessage,
                               request.request_id,
                               StatusCode::InvalidParameter));
  }

  ChatRecord record;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    record = history_.Append(session.username(), content, NowUnixMs());
  }

  HandlerResult result;
  result.to_self.push_back(
      BuildSendMessageResponse(request.request_id, record));
  result.to_broadcast.push_back(BuildNewMessagePush(record));
  return result;
}
```

#### 10.4.5 OnGetHistory

```cpp
HandlerResult ChatService::OnGetHistory(const DecodedRequest& request,
                                        Session& session) {
  // 1. 先解析线格式
  ValueReader reader(request.payload);
  uint64_t before_seq = 0;
  uint16_t limit = 0;

  if (!reader.ReadU64(before_seq) ||
      !reader.ReadU16(limit) ||
      !reader.Done()) {
    return HandlerResult::Response(TlvCodec::MakeError(request.request_id,
                                TlvCodec::ToRawTag(request.tag),
                                StatusCode::MalformedPacket));
  }

  // 2. 登录检查
  if (session.state() != SessionState::Authenticated) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::GetHistory,
                               request.request_id,
                               StatusCode::NotLoggedIn));
  }

  // 3. 参数校验
  if (limit > MaxHistoryLimit) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::GetHistory,
                               request.request_id,
                               StatusCode::InvalidParameter));
  }

  const uint16_t effective_limit =
      limit == 0 ? DefaultHistoryLimit : limit;

  std::vector<ChatRecord> records;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    records = history_.Get(before_seq, effective_limit);
  }

  return HandlerResult::Response(BuildHistoryResponse(request.request_id, records));
}
```

#### 10.4.6 OnGetOnlineUsers

```cpp
HandlerResult ChatService::OnGetOnlineUsers(const DecodedRequest& request,
                                            Session& session) {
  // 1. 先解析线格式：该请求的 payload 必须为空
  ValueReader reader(request.payload);
  if (!reader.Done()) {
    return HandlerResult::Response(TlvCodec::MakeError(request.request_id,
                                TlvCodec::ToRawTag(request.tag),
                                StatusCode::MalformedPacket));
  }

  // 2. 登录检查
  if (session.state() != SessionState::Authenticated) {
    return HandlerResult::Response(MakeSimpleResponse(MessageTag::Resp::GetOnlineUsers,
                               request.request_id,
                               StatusCode::NotLoggedIn));
  }

  std::vector<OnlineUser> users;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    users = online_.Snapshot();
  }

  return HandlerResult::Response(BuildOnlineUsersResponse(request.request_id, users));
}
```

#### 10.4.7 响应构造

```cpp
TlvMessage ChatService::MakeSimpleResponse(MessageTag::Resp::Type tag,
                                           uint32_t request_id,
                                           StatusCode::Type status) const {
  return TlvCodec::MakeResponse(tag, request_id, status);
}

TlvMessage ChatService::BuildUserJoinedPush(
    const std::string& username,
    uint64_t login_timestamp) const {
  std::string body;
  ValueWriter writer(body);
  writer.WriteString(username);
  writer.WriteU64(login_timestamp);

  return TlvCodec::MakePush(MessageTag::Push::UserJoined, body);
}

TlvMessage ChatService::BuildSendMessageResponse(
    uint32_t request_id,
    const ChatRecord& record) const {
  std::string body;
  ValueWriter writer(body);
  writer.WriteU64(record.seq);
  writer.WriteU64(record.timestamp);

  return TlvCodec::MakeResponse(MessageTag::Resp::SendMessage,
                                request_id,
                                StatusCode::Ok,
                                body);
}

TlvMessage ChatService::BuildNewMessagePush(
    const ChatRecord& record) const {
  std::string body;
  ValueWriter writer(body);
  writer.WriteU64(record.seq);
  writer.WriteString(record.sender);
  writer.WriteU64(record.timestamp);
  writer.WriteString(record.content);

  return TlvCodec::MakePush(MessageTag::Push::NewMessage, body);
}
```

#### 10.4.8 历史响应与 payload 截断

```cpp
TlvMessage ChatService::BuildHistoryResponse(
    uint32_t request_id,
    const std::vector<ChatRecord>& records) const {
  // 先编码每条消息；records 已经是 seq 升序
  std::vector<std::string> parts;
  parts.reserve(records.size());

  for (const ChatRecord& record : records) {
    std::string part;
    ValueWriter writer(part);
    writer.WriteU64(record.seq);
    writer.WriteString(record.sender);
    writer.WriteU64(record.timestamp);
    writer.WriteString(record.content);
    parts.push_back(std::move(part));
  }

  // 响应固定部分：request_id(4) + status(2) + count(2)
  size_t total_size = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t);
  for (const std::string& part : parts) {
    total_size += part.size();
  }

  // 超过 MaxPayload 时丢弃最旧的消息（parts 是升序，front 最旧）
  // 保留 seq 较大的新消息，协议要求这样做才能保证翻页不漏消息
  while (total_size > MaxPayload && !parts.empty()) {
    total_size -= parts.front().size();
    parts.erase(parts.begin());
  }

  std::string body;
  ValueWriter writer(body);
  writer.WriteU16(static_cast<uint16_t>(parts.size()));
  for (const std::string& part : parts) {
    writer.WriteBytes(part);
  }

  return TlvCodec::MakeResponse(MessageTag::Resp::GetHistory,
                                request_id,
                                StatusCode::Ok,
                                body);
}
```

#### 10.4.9 在线列表响应

```cpp
TlvMessage ChatService::BuildOnlineUsersResponse(
    uint32_t request_id,
    const std::vector<OnlineUser>& users) const {
  // users 已经按 login_timestamp 升序
  std::vector<std::string> parts;
  parts.reserve(users.size());

  for (const OnlineUser& user : users) {
    std::string part;
    ValueWriter writer(part);
    writer.WriteString(user.username);
    writer.WriteU64(user.login_timestamp);
    parts.push_back(std::move(part));
  }

  size_t total_size = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t);
  for (const std::string& part : parts) {
    total_size += part.size();
  }

  // 超限时丢弃登录时间较早的条目（front）
  while (total_size > MaxPayload && !parts.empty()) {
    total_size -= parts.front().size();
    parts.erase(parts.begin());
  }

  std::string body;
  ValueWriter writer(body);
  writer.WriteU16(static_cast<uint16_t>(parts.size()));
  for (const std::string& part : parts) {
    writer.WriteBytes(part);
  }

  return TlvCodec::MakeResponse(MessageTag::Resp::GetOnlineUsers,
                                request_id,
                                StatusCode::Ok,
                                body);
}
```

#### 10.4.10 Broadcast 与 OnSessionClosed

```cpp
void ChatService::Broadcast(TlvMessage message) {
  std::vector<std::shared_ptr<Session>> targets;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    targets = online_.Sessions();
  }

  for (std::shared_ptr<Session>& session : targets) {
    session->Send(message);
  }
}

void ChatService::OnSessionClosed(const std::string& username,
                                  Session* session) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  online_.RemoveIdentity(username, session);
}
```

---

## 11. Session 详细设计

### 11.1 头文件

```cpp
class ServerContext;

enum class SessionState {
  Unauthenticated,
  Authenticated,
};

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::ip::tcp::socket socket,
          std::shared_ptr<ServerContext> context);
  ~Session();

  void Start();

  // 任意线程可调用：投递到 write_strand_
  void Send(TlvMessage message);

  // 任意线程可调用：先清理状态，再关闭 socket
  void Shutdown();

  // 下面三个方法只在 write_strand_ 上调用
  // （业务 handler 和 process_request 都运行在 write_strand_ 上）
  SessionState state() const;
  const std::string& username() const;
  void SetAuthenticated(std::string username);

 private:
  // ---- 读侧：只在 read_strand_ 上调用 ----
  void read_tag();
  void read_length();
  void read_value(uint16_t len);
  // 读满一包：拷贝 input_msg_ 并投递到 write_strand_，随后立即 read_tag()
  void on_read_completed();

  // ---- 处理与写侧：只在 write_strand_ 上调用 ----
  void process_request(const TlvMessage& request);
  void start_writing();  // 将 write_queue_ 中的字符串发送出去

  void handle_error(const std::error_code& ec);
  void PostShutdown();
  void CloseSocketOnWriteStrand();

  asio::ip::tcp::socket socket_;
  std::shared_ptr<ServerContext> context_;

  // 当出现错误或者异常时，置位 closing_
  std::atomic_bool closing_{false};

  // 读 strand：只在 read_strand_ 上访问
  asio::strand<asio::any_io_executor> read_strand_;
  TlvMessage input_msg_{};

  // 写 strand：只在 write_strand_ 上访问
  asio::strand<asio::any_io_executor> write_strand_;
  std::queue<std::string> write_queue_{};
  bool writing_{false};
  SessionState state_{SessionState::Unauthenticated};
  std::string username_{};
};
```

### 11.2 状态方法

```cpp
SessionState Session::state() const {
  assert(write_strand_.running_in_this_thread());
  return state_;
}

const std::string& Session::username() const {
  assert(write_strand_.running_in_this_thread());
  return username_;
}

void Session::SetAuthenticated(std::string username) {
  assert(write_strand_.running_in_this_thread());
  assert(state_ == SessionState::Unauthenticated);
  state_ = SessionState::Authenticated;
  username_ = std::move(username);
}
```

设计意图：

- 认证状态只允许在 `write_strand_` 上修改；业务 handler 与 `process_request` 都运行在 `write_strand_` 上，因此 `ChatService` handler 可以直接调用这三个方法，不需要跨 strand post。
- 其它线程读取状态必须 post 到 `write_strand_`，不能直接读。
- `SetAuthenticated` 只允许“未登录 → 已登录”单向切换；v0.0.1 不提供登出命令，反向切换不存在。

### 11.3 读循环

保留现有 `read_tag / read_length / read_value` 不变：读满 Value 后先调用 `on_read_completed()` 把消息投递给 `write_strand_`，然后立刻 `read_tag()` 读下一包——读侧不等处理与写完成：

```cpp
void Session::read_value(uint16_t len) {
  assert(read_strand_.running_in_this_thread());
  assert(input_msg_.length() == len);
  (void)len;

  auto self = shared_from_this();
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

          on_read_completed();  // 拷贝 input_msg_ 并 post 到 write_strand_
          read_tag();           // TCP 全双工通信，读写可同时进行
        } else {
          handle_error(ec);
        }
      }));
}
```

### 11.4 完整消息的交接与处理

“读满一包”之后的工作分成两步：`on_read_completed()` 负责把消息从 `read_strand_` 交接给 `write_strand_`，`process_request()` 在 `write_strand_` 上完成业务分发与响应入队。两步分别在不同 strand 上执行。

#### 11.4.1 on_read_completed：从读侧到写侧的交接

```cpp
void Session::on_read_completed() {
  assert(read_strand_.running_in_this_thread());

  /**
   * 这里已经完整地接收到了一条 message 到 input_msg_ 中。
   * input_msg_ 归 read_strand_ 所有，而且紧接着就会被 read_tag()
   * 重新用于读下一包的 Tag / Length，因此必须先把当前包拷贝出来，
   * 再投递到 write_strand_ 上进行业务处理。
   */
  auto self = shared_from_this();
  auto request_msg = std::make_shared<TlvMessage>(input_msg_);

  asio::post(write_strand_, [this, self, request_msg]() {
    if (closing_) return;
    process_request(*request_msg);
    if (!writing_) start_writing();
  });
}
```

设计意图：

- **拷贝是必须的**。`input_msg_` 马上会被下一轮 `read_tag()` 覆盖；用 `shared_ptr` 持有副本，投递的回调执行期间消息一定存活，`DecodedRequest::payload` 指向的 `string_view` 也安全（见 7.3）。
- **投递顺序 == 读包顺序 == 处理顺序**。`read_strand_` 按包的到达顺序 post，`write_strand_` 按 FIFO 执行这些回调，因此流水线请求天然有序。
- **入口检查 `closing_`**。如果回调排队期间连接已经进入关闭流程，直接丢弃本包，不再处理、不再写。
- 处理完再触发写：`process_request` 把响应放进 `write_queue_` 后，由外层 lambda 调用 `start_writing()`（若尚未在写）。

#### 11.4.2 process_request：在 write_strand_ 上处理一个请求

```cpp
void Session::process_request(const TlvMessage& request) {
  assert(write_strand_.running_in_this_thread());
  if (closing_) return;

  HandlerResult result = context_->HandleRequest(*this, request);

  // 如果处理期间连接被顶替/关闭，则丢弃本次结果
  if (closing_) return;

  // 1. 本连接的响应直接入 write_queue_
  //    已经运行在 write_strand_ 上，不需要再经过 Send 的 post
  for (TlvMessage& message : result.to_self) {
    write_queue_.push(std::move(message.SerializeToString()));
  }

  // 2. 广播 Push
  //    Broadcast 内部会调用各在线 Session 的 Send，
  //    Send 再 post 到对应 Session 的 write_strand_
  for (TlvMessage& push : result.to_broadcast) {
    context_->Broadcast(std::move(push));
  }
}
```

顺序设计意图：

- **`to_self` 直接入队**。`process_request` 已经在 `write_strand_` 上，响应序列化后 `push` 进 `write_queue_` 是同步完成的，与后续包的处理之间不存在竞态，也比“经过 `Send` 再 post 一次”少一次投递。
- **`to_broadcast` 通过 `Send` 投递**。对发送者自己，`Send` 的 post 排在当前 `process_request` 之后执行，因此 Push 入队晚于响应，`write_queue_` 顺序为 `[Resp, Push]`，保证“登录响应先于 UserJoinedPush”“发送响应先于 NewMessagePush”。
- **处理期间连接可能被关闭**（例如重复登录顶替、对端断线）。`Shutdown` / `handle_error` 会置位 `closing_`；`process_request` 返回后检查 `closing_`，为真则丢弃本次 `HandlerResult`。即使部分响应已经入队，随后排队的关闭回调也会在 `start_writing` 的 `closing_` 检查下放弃写出。

### 11.5 Send

```cpp
void Session::Send(TlvMessage message) {
  auto self = shared_from_this();

  asio::post(write_strand_, [this, self, message = std::move(message)]() mutable {
    if (closing_) return;

    // SerializeToString 内部会用 value_.size() 同步 length_
    write_queue_.push(message.SerializeToString());

    if (!writing_) {
      start_writing();
    }
  });
}
```

设计意图：

- `Send` 是**广播和跨线程写**的入口：任何线程都能调用，内部统一 post 到目标 Session 的 `write_strand_`。
- 本连接自己的响应不经过 `Send`：`process_request` 已经在 `write_strand_` 上，直接 push 进 `write_queue_`（见 11.4.2）。
- `TlvMessage` 按值捕获，避免调用方栈对象生命周期问题。
- 写失败时由 `start_writing` 的异步写回调调用 `handle_error`。

### 11.6 start_writing

保留当前实现即可：

```cpp
void Session::start_writing() {
  assert(write_strand_.running_in_this_thread());

  if (write_queue_.empty() || closing_) {
    writing_ = false;
    return;
  }

  writing_ = true;
  auto self = shared_from_this();
  const std::string& message = write_queue_.front();

  asio::async_write(
      socket_, asio::buffer(message),
      asio::bind_executor(
          write_strand_,
          [this, self, expected = message.size()](
              std::error_code ec, std::size_t written) {
            (void)written;
            write_queue_.pop();
            writing_ = false;

            if (!ec) {
              assert(expected == written);
              start_writing();
            } else {
              handle_error(ec);
            }
          }));
}
```

### 11.7 Shutdown 与 handle_error

```cpp
void Session::Shutdown() {
  if (closing_.exchange(true)) {
    return;  // 已经在关闭流程中
  }

  INFO("Session {} is shutting down.", (void*)this);
  PostShutdown();
}

void Session::handle_error(const std::error_code& ec) {
  if (closing_.exchange(true)) {
    return;
  }

  ERROR("Session {} error: {}", (void*)this, ec.message());
  PostShutdown();
}

void Session::PostShutdown() {
  auto self = shared_from_this();

  // state_ / username_ 只在 write_strand_ 上访问，
  // socket 关闭也统一在 write_strand_ 上执行，
  // 所以只需要 post 一次：先清理业务状态，再关闭 socket。
  asio::post(write_strand_, [this, self]() {
    if (context_ && state_ == SessionState::Authenticated) {
      context_->OnSessionClosed(*this);
    }

    CloseSocketOnWriteStrand();
  });
}

void Session::CloseSocketOnWriteStrand() {
  assert(write_strand_.running_in_this_thread());

  std::error_code ignored;
  socket_.cancel(ignored);
  socket_.shutdown(asio::socket_base::shutdown_both, ignored);
  socket_.close(ignored);
}
```

设计意图：

- `closing_` 保证关闭流程只启动一次；`Shutdown()` 与 `handle_error()` 殊途同归，都只调用 `PostShutdown()`。
- 清理 `OnlineTable` 需要访问 `state_` / `username_`，两者只在 `write_strand_` 上访问，因此直接 post 到 `write_strand_`。
- 关闭回调与已排队的 `process_request` / `Send` 回调在 `write_strand_` 上按 FIFO 执行：排在前面的回调会在入口检查 `closing_` 并直接返回，不会把响应写进即将关闭的连接。
- `handle_error` 可能本身就运行在 `write_strand_` 上（写错误），`PostShutdown` 仍然统一 post，代码路径只有一条。
- 重复登录顶替旧连接时，业务层在解锁后调用 `old_session->Shutdown()`。

---

## 12. ServerContext 详细设计

### 12.1 头文件

```cpp
class Session;

class ServerContext {
 public:
  ServerContext();

  HandlerResult HandleRequest(Session& session, const TlvMessage& request);
  void Broadcast(TlvMessage push);
  void OnSessionClosed(Session& session);

 private:
  // 注意声明顺序：chat_service_ 必须先于 dispatcher_ 构造，
  // 因为 dispatcher_ 的构造函数会持有 chat_service_ 的引用。
  ChatService chat_service_;
  Dispatcher dispatcher_;
};
```

### 12.2 实现

```cpp
ServerContext::ServerContext()
    : dispatcher_(chat_service_) {}

HandlerResult ServerContext::HandleRequest(Session& session,
                                           const TlvMessage& request) {
  return dispatcher_.Dispatch(session, request);
}

void ServerContext::Broadcast(TlvMessage push) {
  chat_service_.Broadcast(std::move(push));
}

void ServerContext::OnSessionClosed(Session& session) {
  chat_service_.OnSessionClosed(session.username(), &session);
}
```

设计意图：

- `ServerContext` 不写业务逻辑，只是把 `Dispatcher` 和 `ChatService` 组合起来。
- Session 只依赖这个类，不需要知道 `Dispatcher` / `ChatService` 存在。
- `HandleRequest` 由 `Session::process_request` 在调用方的 `write_strand_` 上同步执行（见 6.2 / 11.4.2）。
- `OnSessionClosed` 由 `Session::PostShutdown` 投递到 `write_strand_` 的回调调用，此时访问 `session.username()` 是安全的（见 6.4 / 11.7）。
- 未来加定时器、配置、统计时，都在 `ServerContext` 扩展。

---

## 13. Acceptor 和 main 接线

### 13.1 Acceptor

```cpp
class Acceptor {
 public:
  Acceptor(IoThreads& io_threads,
           const std::string& ip,
           uint16_t port,
           std::shared_ptr<ServerContext> context);

 private:
  void do_accept();

  asio::ip::tcp::endpoint endpoint_;
  asio::ip::tcp::acceptor acceptor_;
  std::shared_ptr<ServerContext> context_;
};
```

```cpp
Acceptor::Acceptor(IoThreads& io_threads,
                   const std::string& ip,
                   uint16_t port,
                   std::shared_ptr<ServerContext> context)
    : endpoint_(asio::ip::make_address(ip), port),
      acceptor_(io_threads.GetIoContext(), endpoint_),
      context_(std::move(context)) {
  INFO("Acceptor is listening in {}:{}", ip, port);
  do_accept();
}

void Acceptor::do_accept() {
  acceptor_.async_accept(
      [this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
          std::make_shared<Session>(std::move(socket), context_)->Start();
        } else {
          ERROR("Acceptor error occurred: {}", ec.message());
        }
        do_accept();
      });
}
```

### 13.2 main

```cpp
int main(int argc, char* argv[]) {
  try {
    auto context = std::make_shared<ServerContext>();

    IoThreads io_threads(2);
    Acceptor acceptor(io_threads, "127.0.0.1", 35565, context);

    io_threads.Run();
  } catch (std::exception& e) {
    CRITICAL("Exception ocurred in main function: {}", e.what());
  }
  return 0;
}
```

---

## 14. 关键流程详解

> 所有 handler 都遵循同一个顺序：**先解析线格式 → 再检查 Session 状态 → 再做语义校验 → 最后访问共享状态。**

### 14.1 注册

```text
Client
  → Session::read_value 完成 → on_read_completed()          [read_strand_]
      拷贝 input_msg_ → shared_ptr<TlvMessage>
      asio::post(write_strand_, 处理回调)
  → Session::read_tag()：立即读下一包，不等处理               [read_strand_]
  → Session::process_request(request)                       [write_strand_]
  → Dispatcher::Dispatch
      raw_tag = 0x0001
      request_id 提取成功且非 0
      handler = ChatService::OnRegister
  → ChatService::OnRegister
      1. ValueReader 解析 username/password；失败则 Resp::Error(MalformedPacket)
      2. 已登录则 Resp::Register(InvalidState)
      3. 长度与 UTF-8 校验；失败则 InvalidParameter
      4. lock(state_mutex_)
      5. users_.Register
      6. unlock
  → HandlerResult.to_self = [RegisterResp]
  → RegisterResp.SerializeToString() 直接 push 进 write_queue_  [write_strand_]
  → if (!writing_) start_writing()
```

### 14.2 登录

```text
Client
  → on_read_completed → post(write_strand_) → read_tag() 继续读   [read_strand_]
  → process_request → ChatService::OnLogin                     [write_strand_]
      1. ValueReader 解析 username/password；失败则 Resp::Error(MalformedPacket)
      2. 已登录则 Resp::Login(InvalidState)
      3. 长度与 UTF-8 校验；失败则 InvalidParameter
      4. lock(state_mutex_)
      5. users_.Verify
      6. online_.Put，得到 old_session
      7. unlock
      8. session.SetAuthenticated(username)  // 直接修改，已在 write_strand_ 上
      9. old_session->Shutdown()             // 顶替旧连接；投递到旧 Session 的 write_strand_
     10. to_self = [LoginResp(Ok)]
     11. to_broadcast = [UserJoinedPush]
  → LoginResp 直接 push 进 write_queue_
  → ServerContext::Broadcast(UserJoinedPush) → 各在线 Session::Send(push)
  → if (!writing_) start_writing()
```

重复登录的竞态处理：

- `online_.Put` 已经把表项替换为新 Session。
- 旧 Session 稍后在 `PostShutdown` 的回调中调用 `OnSessionClosed`。
- `OnlineTable::RemoveIdentity` 发现表项已经是新 Session，因此不会删除。

### 14.3 发送消息

```text
Client
  → on_read_completed → post(write_strand_) → read_tag() 继续读   [read_strand_]
  → process_request → ChatService::OnSendMessage                [write_strand_]
      1. ValueReader 解析 content；失败则 Resp::Error(MalformedPacket)
      2. 未登录则 Resp::SendMessage(NotLoggedIn)
      3. 内容长度与 UTF-8 校验；失败则 InvalidParameter
      4. lock(state_mutex_)
      5. history_.Append(username, content, now)
      6. unlock
      7. to_self = [SendMessageResp(Ok, seq, timestamp)]
      8. to_broadcast = [NewMessagePush]
  → SendMessageResp 直接 push 进 write_queue_
  → ServerContext::Broadcast(NewMessagePush) → 各在线 Session::Send(push)
  → if (!writing_) start_writing()
```

### 14.4 获取历史

```text
Client
  → on_read_completed → post(write_strand_) → read_tag() 继续读   [read_strand_]
  → process_request → ChatService::OnGetHistory                 [write_strand_]
      1. ValueReader 解析 before_seq / limit；失败则 Resp::Error(MalformedPacket)
      2. 未登录则 Resp::GetHistory(NotLoggedIn)
      3. limit 校验；失败则 InvalidParameter；0 使用默认值 50
      4. lock(state_mutex_)
      5. history_.Get(before_seq, effective_limit)
      6. unlock
      7. BuildHistoryResponse：
           - 先编码全部消息
           - 超出 65535 时从最旧消息开始丢弃
           - count 写实际返回数量
  → GetHistoryResp 直接 push 进 write_queue_
  → if (!writing_) start_writing()
```

### 14.5 获取在线列表

```text
Client
  → on_read_completed → post(write_strand_) → read_tag() 继续读   [read_strand_]
  → process_request → ChatService::OnGetOnlineUsers             [write_strand_]
      1. payload 必须为空；否则 Resp::Error(MalformedPacket)
      2. 未登录则 Resp::GetOnlineUsers(NotLoggedIn)
      3. lock(state_mutex_)
      4. online_.Snapshot()
      5. unlock
      6. BuildOnlineUsersResponse：
           - 超出 65535 时丢弃登录较早的用户
  → GetOnlineUsersResp 直接 push 进 write_queue_
  → if (!writing_) start_writing()
```

## 15. 错误处理

### 15.1 Dispatcher 层

| 输入 | 返回 |
|------|------|
| Tag < 0x0001 或 Tag > 0x00FF | `Resp::Error(request_id=0, request_tag, MalformedPacket)` |
| Req 区间，Value < 4 字节 | `Resp::Error(request_id=0, request_tag, MalformedPacket)` |
| Req 区间未知 Tag | `Resp::Error(提取的 request_id, request_tag, MalformedPacket)` |
| 已知 Tag，request_id == 0 | 对应 Resp，`status=InvalidParameter` |
| handler 抛异常 | 对应 Resp，`status=InternalError` |

### 15.2 ChatService 层

| 输入 | 返回 |
|------|------|
| 已登录再 Register/Login | 对应 Resp，`status=InvalidState` |
| 未登录访问受保护接口 | 对应 Resp，`status=NotLoggedIn` |
| Value 字段不足 / 多余 / String 越界 | `Resp::Error(..., MalformedPacket)` |
| 用户名、密码、limit、content 语义不合法 | 对应 Resp，`status=InvalidParameter` |
| 注册重名 | `Resp::Register(status=UsernameAlreadyExists)` |
| 登录凭据错误 | `Resp::Login(status=InvalidCredentials)` |

### 15.3 连接层

| 事件 | 行为 |
|------|------|
| EOF / 网络错误 | `handle_error` → `PostShutdown` post 到 `write_strand_` → 清理 OnlineTable → close socket |
| 重复登录顶替 | 旧 Session 调 `Shutdown()`；已入队的处理回调检查 `closing_` 后丢弃，未完成请求不再处理 |
| 处理期间连接被关闭 | `process_request` 返回后检查 `closing_`，丢弃本次 `HandlerResult` |
| 写错误 | 写 strand 回调 `handle_error` |
| 读错误 | 读 strand 回调 `handle_error` |

---

## 16. 需要重点测试的边界

1. **Tag 边界**
   - `0x0000`、`0x0006`、`0x8000`、`0x8101` 都不能被客户端当作请求。

2. **request_id**
   - Value 长度 0、1、3 时，`request_id` 回传 0。
   - Value 长度 4 时，`request_id` 能正确回传。
   - `request_id = 0` 的已知请求返回 `InvalidParameter`。

3. **注册/登录**
   - 正常注册和重复注册。
   - 登录成功后的连接状态切换。
   - 登录后再次 Login 返回 `InvalidState`。
   - 错误密码返回 `InvalidCredentials`。

4. **流水线**
   - 一次发送 Login + SendMessage + GetHistory，响应仍能按 request_id 对应。
   - 读侧不等待处理：消息连续到达时 `read_tag()` 持续读下一包，处理与写在 `write_strand_` 上串行推进。
   - 协议不要求响应顺序严格等于发送顺序，但当前实现（处理与写共用 `write_strand_`）保证响应按请求顺序写回。

5. **Push 顺序**
   - 登录者先收到 `LoginResp`，再收到 `UserJoinedPush`。
   - 发送者先收到 `SendMessageResp`，再收到 `NewMessagePush`。

6. **历史翻页**
   - 消息数超过 limit 时能正确翻页。
   - 单条消息接近 65535 时，响应截断后不会漏消息。

7. **重复登录**
   - 新连接登录成功后，旧连接收到 EOF。
   - 旧连接关闭不会把新连接从 OnlineTable 删除。

8. **断线**
   - 已登录连接 EOF 后，在线列表不再包含该用户。

---

## 17. 实施顺序

### Phase 1：基础接线

- 新增 `protocol/request.h`：`DecodedRequest`、`HandlerResult`。
- 修改 `TlvMessage::SerializeToString`，自动同步 `length_`。
- 新增 `ServerContext`，先不实现业务，`HandleRequest` 可返回固定错误。
- 修改 `Acceptor` 构造函数，持有 `shared_ptr<ServerContext>`。
- 修改 `Session`：
  - 构造时接收 `ServerContext`；
  - 增加 `SessionState`、`username_`；
  - 把 `on_read_completed` 投递回调中的回显逻辑替换为 `process_request`，内部调用 `context_->HandleRequest`。

验收：

- 编译通过。
- 现有 TCP 回显能力不退化（可临时保留回显 handler）。

### Phase 2：TlvCodec

- 实现 `ValueReader` / `ValueWriter`。
- 实现字节序工具和 UTF-8 校验。
- 实现 `MakeResponse` / `MakeError` / `MakePush`。
- 实现 `PeekRequestId` / `PayloadAfterRequestId`。

验收：

- 单元测试覆盖边界读取、String 越界、大端转换。

### Phase 3：Dispatcher

- 实现方向检查、request_id 提取、未知 Tag 错误。
- 先注册 5 个空 handler，每个返回固定响应。
- `Session::process_request` 接入 Dispatcher。

验收：

- 发送非法 Tag 得到 `Resp::Error`。
- 发送合法 Tag 得到 request_id 正确回传的响应。

### Phase 4：注册与登录

- 实现 `UserRegistry`、`OnlineTable`。
- 实现 `ChatService::OnRegister`、`OnLogin`。
- 实现 `Session::SetAuthenticated`、`Shutdown`。
- 实现 `Push::UserJoined` 广播。

验收：

- 注册、登录、重复注册、错误密码、重复登录顶替旧连接。

### Phase 5：聊天与历史

- 实现 `HistoryStore`。
- 实现 `OnSendMessage`、`OnGetHistory`、`OnGetOnlineUsers`。
- 实现 `Push::NewMessage` 广播。
- 实现历史响应 payload 截断。

验收：

- 发消息后所有在线连接收到 Push。
- 历史翻页正确，在线列表正确。

### Phase 6：收尾

- 补充日志。
- 更新 `test/TestCMD.md`。
- 增加错误注入和并发测试。
- 复查锁顺序和关闭竞态。

---

## 18. 待确认决策

1. **同步 handler 还是 logic strand**
   当前设计是 `write_strand_` 同步 handler + `ChatService` 内部 mutex。若未来业务变重，迁移到全局 logic strand。

2. **密码存储**
   当前明文。TLS 和密码哈希是否在 v0.0.2 一起做？

3. **历史上限**
   当前不限制。是否在 `HistoryStore` 加 `MaxHistorySize`？

4. **在线列表分页**
   当前超 payload 时丢弃登录较早用户且无翻页参数。是否需要分页？

5. **重复登录策略**
   当前是“新连接顶替旧连接”。是否在旧连接被顶替时发送一个明确的 Push？

6. **`SerializeToString` 自动同步 `length_`**
   建议采纳。若要保持 `TlvMessage` 更纯粹的容器语义，也可改为 `Send` 中显式 `set_length`。
