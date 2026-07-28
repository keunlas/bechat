# BeChat v0.0.1 设计文档

## 1. 概述

BeChat 是一个使用 C++ 编写的聊天服务器，v0.0.1 为最小可行版本。

**技术选型：**

- **框架**：Asio (non-boost standalone)
- **协议**：自定义固定长度二进制 TLV over TCP
- **存储**：纯内存，无持久化，无外部数据库
- **并发**：多 io_context 多线程 + Asio strand

**功能范围：**

- 用户注册 / 登录
- 1 对 1 私聊（在线实时推送 + 离线消息拉取）
- 群聊（创建/加入/离开房间 + 房间内广播）
- 仅提供基础抽象接口，主要业务逻辑由客户端设计

---

## 2. 整体架构

### 2.1 模块划分

```
┌─────────────────────────────────────────────────────┐
│                    BeChat Server                     │
├──────────┬──────────┬──────────┬────────────────────┤
│ Listener │ Session  │ Session  │  ... (per connect)  │
│ (acceptor)│(strand)  │(strand)  │                    │
├──────────┴──────────┴──────────┴────────────────────┤
│                    IoContextPool                     │
│    io_context[0]   io_context[1]  ... io_context[N]  │
│    thread[0]        thread[1]         thread[N]       │
├─────────────────────────────────────────────────────┤
│                    Shared State                      │
│  UserManager  │  RoomManager  │  MessageStore        │
│  (在线/离线用户)│  (群聊房间)    │  (离线消息队列)       │
├─────────────────────────────────────────────────────┤
│                    Protocol Layer                    │
│   TlvCodec (编解码)  │  Dispatcher (消息路由)         │
└─────────────────────────────────────────────────────┘
```

### 2.2 模块职责

| 模块 | 职责 | 线程安全策略 |
|------|------|-------------|
| **IoContextPool** | 管理 N 个 io_context + N 个工作线程，round-robin 分配连接 | 构造后只读 |
| **Listener** | Accept TCP 连接，创建 Session，分配到某个 io_context | 单 strand |
| **Session** | 一个客户端连接的生命周期：读 TLV → Dispatch → 写响应 | 绑定到自己的 strand |
| **TlvCodec** | 固定长度二进制 TLV 的编码/解码 | 无状态，线程安全 |
| **Dispatcher** | 根据 TLV Type 路由到对应 handler | 无状态 |
| **UserManager** | 用户注册、登录登出、在线状态查询 | 绑定一个共享 strand |
| **RoomManager** | 群聊房间创建/销毁、成员管理 | 绑定一个共享 strand |
| **MessageStore** | 离线消息入队/出队 | 绑定一个共享 strand |

### 2.3 线程模型

- N 个 `std::thread`，每个 run 一个 `io_context`
- 每个 Session 创建时分配到一个 io_context，并绑定自己的 strand
- 共享状态组件共用一个共享 strand，外部通过 `strand.post()` / `asio::dispatch(strand, ...)` 访问
- 推送消息给在线用户时通过 `asio::dispatch(target_session->strand(), ...)` 安全投递

### 2.4 数据流示例（发送私聊消息）

```
Client_A --[TLV(SendPrivateMsg)]--> Session_A --post--> Dispatcher
  --> UserManager.lookup(B) --> 在线?
      --> 是: asio::dispatch(Session_B.strand, PushMessage)
      --> 否: asio::dispatch(shared_strand, MessageStore.enqueue(B, msg))
```

---

## 3. TLV 协议设计

### 3.1 基础格式

```
┌────────┬──────────┬──────────────────┐
│ Type   │ Length   │ Value            │
│ 2 bytes│ 4 bytes  │ Length bytes     │
│ uint16 │ uint32   │ variable         │
└────────┴──────────┴──────────────────┘
```

- **Type**：uint16，标识操作类型
- **Length**：uint32，Value 部分的字节数
- **Value**：二进制 payload，每个 Type 有固定内部结构

### 3.2 v0.0.1 命令清单

| Type | 名称 | 方向 | 说明 |
|------|------|------|------|
| `0x0001` | **Register** | C→S | 注册新用户 |
| `0x0002` | **Login** | C→S | 登录，成功后 Session 变为 authenticated |
| `0x0010` | **SendPrivateMsg** | C→S | 发送私聊消息 |
| `0x0011` | **SendGroupMsg** | C→S | 发送群聊消息 |
| `0x0020` | **JoinRoom** | C→S | 加入/创建群聊房间 |
| `0x0021` | **LeaveRoom** | C→S | 离开群聊房间 |
| `0x0100` | **PullOfflineMsg** | C→S | 拉取所有离线消息 |

> S→C 方向的响应和推送复用同一套 Type，客户端根据上下文区分。

### 3.3 各命令 Value 结构

**Register：**

```
username     [32 bytes, 固定长度, ASCII/UTF-8]
password     [64 bytes, 固定长度, hash]
```

**Login：**

```
username     [32 bytes]
password     [64 bytes]
```

**SendPrivateMsg / SendGroupMsg：**

```
target       [32 bytes, 对方用户名 或 房间名]
content      [N bytes, 剩余部分]
```

**JoinRoom / LeaveRoom：**

```
room_name    [32 bytes]
```

**PullOfflineMsg：**

```
(空 Value)
```

### 3.4 响应与推送结构

所有 S→C 方向消息的 Value 前 2 bytes 固定为 `status_code`（`0x0000`=成功，其他=错误码）。

**LoginResp：**

```
status       [2 bytes]
user_id      [4 bytes, 预留]
```

**NewMsgPush（私聊/群聊消息推送）：**

```
status       [2 bytes, 恒为 0x0000]
sender       [32 bytes]
content      [N bytes]
```

**PullOfflineMsgResp：**

```
status       [2 bytes]
count        [2 bytes, 消息条数]
messages:
  room      [1 byte, 0=私聊, 1=群聊]
  sender    [32 bytes]
  content   [N bytes]
  ... (重复 count 次)
```

### 3.5 用户标识

用户名固定 32 字节（ASCII/UTF-8），作为用户主键。v0.0.1 不需要数字 ID。

---

## 4. 共享状态设计

UserManager、RoomManager、MessageStore 共同绑定一个共享 strand，所有外部访问通过 strand 投递，保证线程安全。

### 4.1 UserManager

```
数据结构:
  users_:  unordered_map<username, UserRecord>
  online_sessions_: unordered_map<username, Session*>

UserRecord:
  username       (32 bytes, key)
  password_hash  (64 bytes)
  created_at     (uint64, unix timestamp)
```

| 接口 | 说明 |
|------|------|
| `Register(username, password_hash)` → status | 检查重名，插入 users_ |
| `Login(username, password_hash, session)` → status | 验证凭据；若已有活跃 Session 返回错误 "重复登录" |
| `Logout(username)` | 从 online_sessions_ 移除 |
| `Lookup(username)` → Session* | 查在线 Session，NULL = 离线 |
| `Exists(username)` → bool | 用户是否存在 |

### 4.2 RoomManager

```
数据结构:
  rooms_: unordered_map<room_name, Room>

Room:
  name:     string (32 bytes)
  members:  unordered_set<username>
```

| 接口 | 说明 |
|------|------|
| `Join(room_name, username)` → status | 不存在则创建房间，将 username 加入 members；已在房间返回错误 |
| `Leave(room_name, username)` → status | 从 members 移除，空房间自动销毁；不在房间返回错误 |
| `GetMembers(room_name)` → members 集合 | 用于群聊消息广播 |
| `Exists(room_name)` → bool | 房间是否存在 |

### 4.3 MessageStore

```
数据结构:
  offline_queues_: unordered_map<username, deque<Message>>

Message:
  sender:     username (32 bytes)
  content:    string
  timestamp:  uint64
  room:       string (空=私聊, 非空=群聊)
```

| 接口 | 说明 |
|------|------|
| `Enqueue(to_username, msg)` | 追加到 to_username 的离线队列 |
| `DequeueAll(username)` → vector<Message> | 取出所有离线消息并清空队列 |
| `Count(username)` → size_t | 离线消息数量 |

---

## 5. Session 生命周期与连接管理

### 5.1 状态机

```
              ┌──────────┐
 TCP accept → │   IDLE   │ ← 连接刚建立，未认证
              └────┬─────┘
                   │ Register/Login 成功
              ┌────▼─────┐
              │  ACTIVE  │ ← 已认证，可收发消息
              └────┬─────┘
                   │ Logout / TCP close
              ┌────▼──────┐
              │  CLOSED   │ ← 资源回收
              └───────────┘
```

| 状态 | 可接收的命令 | 行为 |
|------|------------|------|
| IDLE | Register, Login | 其他命令返回 `0x0001`（未认证） |
| ACTIVE | 所有聊天命令 | 正常处理 |
| CLOSED | 无 | 对象待销毁 |

### 5.2 读循环（async read loop）

```
Session::Start()            // Listener accept 后调用
  → ReadHeader()            // async_read 6 字节头部
    → OnHeaderRead()
      → ReadBody()          // async_read Length 字节 Body
        → OnBodyRead()
          → Dispatcher::Dispatch(type, body)
          → ReadHeader()    // 继续读下一条消息
```

整条链在同一 strand 内串行执行。

### 5.3 写操作

- Session 维护 `deque<Buffer>` 发送队列
- `Session::Send(tlv_data)` 入队，若队列之前为空则触发 `async_write`
- `async_write` 完成后检查队列，继续发送或等待
- 推送消息通过 `asio::dispatch(session_strand, ...)` 安全注入写队列

### 5.4 连接断开处理

1. Session 的 strand 收到 `asio::error::eof` 或错误
2. 若 ACTIVE 状态：调用 `UserManager::Logout(username)` 移除在线记录
3. 关闭 socket
4. Session 由 `enable_shared_from_this` 管理，引用计数归零后自动销毁

---

## 6. 错误处理与状态码

### 6.1 状态码定义

| 状态码 | 含义 |
|--------|------|
| `0x0000` | 成功 |
| `0x0001` | 未认证（IDLE 状态发送聊天命令） |
| `0x0002` | 用户名已存在（Register） |
| `0x0003` | 用户名或密码错误（Login） |
| `0x0004` | 用户不存在（发送消息给不存在的用户） |
| `0x0005` | 房间不存在（Join 前发送群聊消息） |
| `0x0006` | 重复登录（同一用户已有活跃 Session） |
| `0x0007` | TLV 格式错误（Length 不匹配、非法 Type） |
| `0x0008` | 已在房间中（重复 Join） |
| `0x0009` | 不在房间中（未 Join 就 Leave 或发群聊消息） |
| `0xFFFF` | 服务器内部错误 |

> 状态码作为 S→C 方向 Value 的前 2 bytes。

### 6.2 错误处理策略

| 场景 | 处理方式 |
|------|---------|
| 非法 TLV（Length 不匹配、未知 Type） | 返回错误 + 关闭连接 |
| 未认证发命令 | 返回 `0x0001`，保持连接 |
| 业务逻辑错误（重复注册、不存在的房间等） | 返回对应错误码，保持连接 |
| TCP 断连 | 清理在线状态，销毁 Session |
| 内部异常（OOM 等） | 关闭对应连接，记录日志 |

### 6.3 离线队列

- 离线队列不设上限（v0.0.1 内存足够）
- `PullOfflineMsg` 取出后清空
- 后续版本可扩展 `max_offline_per_user` 限制
