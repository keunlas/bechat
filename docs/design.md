# BeChat 设计文档（下一步）

## 1. 文档目的

本设计面向 bechat 当前代码（基于 Asio 的 TCP/TLV echo 服务器），给出“下一步如何设计”的整体方案。文档回答三个问题：

1. 当前代码处于什么状态，距离可用还有哪些差距；
2. BeChat 最终应该长什么样（连接模型、内存状态、模块、线程模型、协议）；
3. 按什么顺序、分几个阶段实现（详见 [roadmap.md](roadmap.md)）。

## 2. 现状分析

### 2.1 已完成的能力

- CMake 工程骨架：测试、示例、安装、CPack 选项齐全；
- standalone Asio 异步 TCP 服务器：`Listener` accept、`Session` 异步读；
- 自定义 TLV 数据类 `TlvMessage`（Type u16 + Length u16 + Value）；
- 多线程 io_context（单 io_context + 2 个工作线程）；
- spdlog 异步控制台日志封装 `Log`；
- 最小验证命令（`nc` 发送 TLV，服务端原样回显）。

### 2.2 主要差距与风险

| 现状 | 问题 | 影响 | 建议 |
|------|------|------|------|
| Session 持续读循环并回显 | 当前模型是长连接 echo，与“单命令短连接”目标不符 | 无法承载注册、登录、消息等原语 | 改为单请求短连接：读一条 → 响应 → 关闭，保留读循环结构供未来长连接复用 |
| Length 为 u16 且无上限校验 | 恶意客户端可反复声明大包 | 内存/带宽滥用风险 | 保持 u16（既定决策），补 `max_payload` 校验并拒绝异常 Length |
| `read_value()` 使用 `assert` 校验 | NDEBUG 下校验消失 | 生产行为不确定 | 去掉 assert，改为显式错误处理 |
| 单 io_context + 多线程，Session 无 strand | 并发读写时同一 Session 的处理会并发执行 | 数据竞争、写交错 | 建立 IoContextPool + 每 Session 一个 strand |
| 只有“读完才回写”的单次写 | 没有写队列，响应与未来推送无法组合 | 扩展性差 | Session 增加发送队列，串行 async_write |
| 出错后静默停止，不关闭、不清理 | 没有统一的连接关闭路径 | 连接泄漏、日志缺失 | 统一 Close 流程：日志、清理、关 socket |
| 端口 35565、线程数 2 硬编码 | 不可配置 | 部署不灵活 | 命令行参数 + 简单配置 |
| 无优雅停机 | Ctrl-C 直接终止进程 | 状态清理、日志完整性差 | signal_set + 关闭 acceptor/udp + 关闭在途 Session + join |
| spdlog/asio 依赖未在 CMake 中显式声明 | 依赖系统头文件，环境不同即编译失败 | 可移植性差 | find_package + FetchContent 回退 |
| test/ 下只有 CMake 脚手架，无测试 | 协议改动无回归保障 | 改动协议风险高 | 先补 codec/dispatcher 单元测试与集成测试 |
| `TlvMessage` 只是数据容器，无编解码能力 | 序列化/反序列化逻辑散落在 Session | 复用性差、易出错 | 增加 TlvCodec 与消息模型 |
| 无身份、在线与心跳机制 | 无法判断“谁在线上” | 无法实现聊天原语 | 增加 AuthService、OnlineTracker 与 UDP 心跳接收器 |
| 无私聊待收队列与轮询接口 | 短连接模型下服务端无法主动投递 | 私聊消息无法送达 | 增加内存待收队列与 Poll 原语 |
| 无群聊与历史能力 | 群聊消息无法在服务端留存 | 无法实现群聊历史回补 | 增加 GroupManager 与内存历史存储 |
| 旧设计文档与当前需求冲突 | 旧文档采用长连接 + 服务端推送模型 | 方向错误 | 本目录文档已按当前需求重写 |

## 3. 设计原则

以 [docs/outline.md](outline.md) 为准则，逐条明确其含义：

1. **使用 Asio（non-boost）为基本框架**：只依赖 standalone Asio，网络层全部异步化。
2. **使用自定义 TLV 协议**：固定头部 + 类型化载荷，协议定义见 [protocol.md](protocol.md)。
3. **用户数据以及聊天数据存储在内存中**：用户注册信息、在线信息、群聊历史都存在服务器内存。
4. **暂不引入数据库或其他组件**：不引入 Redis、MySQL、消息队列等外部组件。
5. **暂不考虑数据持久化**：服务器重启后内存状态全部清空，客户端自行处理重连与数据恢复。
6. **BeChat 只提供基础的单独的抽象的接口**：注册、登录、发消息、群聊、拉历史等是独立原语；“登录后自动加入群”“发消息失败后重试”这类编排逻辑属于客户端。
7. **内存有状态、但不持久化**：
   - BeChat 在内存中持有：**用户注册信息、在线用户信息、群聊历史消息**，以及在线用户的**私聊待收队列**（投递缓冲，非历史）；
   - **私聊消息不作为历史保存**，仅短暂缓存在接收方的待收队列中，Poll 拉取后即清空；
   - 所有状态只存在于内存，重启即清空，不依赖外部组件。
8. **单请求短连接（类似 HTTP）**：
   - 每个 TCP Session **只处理一条命令**，服务器响应后即关闭连接；
   - **用户是否在线完全由 UDP 心跳判定**，与 TCP 连接状态无关；
   - 服务端不主动推送，客户端通过 Poll / PullGroupHistory 拉取消息；
   - 协议**保留长连接的可能性**：预留连接协商原语与 Push 类型，未来可平滑增加长连接模式。

### 3.1 状态归属

| 状态类型 | 归属 | 说明 |
|----------|------|------|
| 用户注册信息（用户名、密码哈希等） | 服务器内存 | 唯一性校验必须由服务端完成 |
| 在线用户信息（用户名、最近心跳等） | 服务器内存 | 完全由 UDP 心跳维护，30 秒无心跳即离线 |
| 私聊待收队列 | 服务器内存 | 仅在线用户有；接收方 Poll 后清空，离线/超时丢弃 |
| 群聊历史消息 | 服务器内存 | 按群保存、有上限、可回补 |
| 业务编排（联系人、已加入群列表、本地消息历史、重试策略） | 客户端 | 客户端自行记录与维护 |

## 4. 内存状态模型

| 状态 | 数据结构 | 生命周期 | 上限与清理 |
|------|----------|----------|------------|
| UserRegistry | `unordered_map<username, UserRecord>`；UserRecord = {password_hash, salt, created_at} | 注册时创建；重启清空 | `max_users`（默认 10000），达到上限拒绝注册 |
| OnlineTable | `unordered_map<username, OnlineEntry>`；OnlineEntry = {username, token 摘要, last_heartbeat(steady_clock), login_at, pending_private(deque)} | Login 登记；Logout 或心跳超时（30 秒）移除 | `max_online`（默认 4096）；私聊待收队列 `max_pending_private`（默认 100 条/人），超出逐出最旧 |
| GroupStore | `unordered_map<group_name, Group>`；Group = {name, owner, members, messages(deque), next_seq} | 创建/加入产生；重启清空；不提供删群 | `max_groups`（默认 1000）；每组历史 `max_group_history`（默认 1000 条），超出逐出最旧 |

说明：

- 密码只存哈希（加盐），不存明文；
- 心跳时间使用 `steady_clock`，不受系统时钟跳变影响；
- 私聊待收队列是**投递缓冲**而非历史：接收方 Poll 一次即清空；接收方离线或心跳超时则丢弃；
- 群聊历史消息带单调递增的 `seq`，客户端按 `seq` 增量拉取；
- 在线表不保存任何 TCP 会话引用——TCP 每次使用即断开，与在线状态无关。

## 5. 目标架构

### 5.1 模块总览

```
┌─────────────────────────────────────────────────────────────┐
│                        bechatd（服务端进程）                   │
│                                                              │
│  ┌────────────────┐    ┌──────────────────────────────┐     │
│  │  Listener      │    │  UdpHeartbeatReceiver        │     │
│  │  (TCP acceptor)│    │  (UDP 心跳，同端口或独立端口)  │     │
│  └───────┬────────┘    └──────────────┬───────────────┘     │
│          │ accept                     │ datagram             │
│          ▼                            ▼                      │
│  ┌────────────────────────────────────────────────────┐     │
│  │             IoContextPool（N × io_context）          │     │
│  │  io[0]：Listener / UDP / 定时器 / logic strand      │     │
│  │  io[1..N-1]：TCP Session（单请求短连接，各自 strand）│     │
│  └──────────────────────┬─────────────────────────────┘     │
│                         ▼                                    │
│  ┌────────────────────────────────────────────────────┐     │
│  │ Protocol 层：TlvCodec · Dispatcher                  │     │
│  │ Service 层：AuthService · OnlineTracker ·           │     │
│  │            GroupManager · RelayService             │     │
│  └────────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 模块职责

| 模块 | 职责 | 并发策略 |
|------|------|----------|
| `IoContextPool` | 管理 N 个 io_context 与 N 个工作线程；TCP 连接 round-robin 分配 | 构造后只读 |
| `Listener` | accept TCP 连接，创建 Session 并 Start | 单条 accept 链，天然串行 |
| `UdpHeartbeatReceiver` | 绑定 UDP 端口，接收心跳包，解析并更新在线表心跳时间 | 单一接收循环，投递到 logic strand |
| `Session` | 短连接生命周期：读取一条命令 → 分发 → 写响应 → 关闭；预留长连接模式 | 每 Session 一个 strand |
| `TlvCodec` | 报文头解析、Length 校验、消息对象编解码 | 无状态纯函数，线程安全 |
| `Dispatcher` | 按 Type 查表调用 handler；未知 Type 返回协议错误 | 无状态 |
| `AuthService` | 注册/登录校验、HMAC 令牌签发与验证 | logic strand |
| `OnlineTracker` | 在线表维护、心跳更新、30 秒超时扫描、登录/登出 | logic strand（含定时器） |
| `GroupManager` | 群创建/加入/离开、成员校验、群聊历史写入与拉取 | logic strand |
| `RelayService` | 私聊入队（仅在线接收方）、Poll 出队、群历史查询配合 | logic strand |

## 6. 线程模型

- `IoContextPool`：N 个 `io_context`，各跑一个 `std::thread`；
- 每个 TCP Session 在 accept 时分配到某个 io_context，并创建自己的 `strand`；
- UDP 心跳接收器、心跳超时定时器、Listener 与所有共享服务都挂在 `io_context[0]` 上，共享一个 logic strand；
- 跨线程操作一律通过 `asio::post` / `asio::dispatch` 投递，不共享裸指针读写。

规则：

1. Session 的所有异步操作（读、写、关闭）都经过自身 strand；
2. 共享状态（UserRegistry、OnlineTable、GroupStore）只在 logic strand 上访问；
3. UDP 心跳包到达后，接收器只做解析，然后 `post` 到 logic strand 更新在线表；
4. 业务 handler 通过 `post(session->strand(), ...)` 投递响应写出（当前连接短命，主要是关闭时序控制）；
5. 不在 handler 中做阻塞调用。

## 7. 核心机制

### 7.1 单请求处理流程

```
accept()
  → read_header()          // 固定 4 字节头（Type u16 + Length u16）
    → 校验 Type/Length     // 非法则发送错误响应并关闭
    → read_value(len)      // 异步读取 len 字节
      → codec.Parse()      // 解析成请求对象
      → dispatcher.Dispatch()
      → write_response()   // 串行写出响应
      → close()            // 写完后关闭连接
```

每条 TCP 连接只承载一个请求。响应写完后服务器主动关闭；若客户端在响应前多发了数据，多余字节被忽略。

> 实现上保留“读循环”代码结构：`Session` 增加 `mode_` 字段，当前固定为单请求模式；未来长连接模式启用时，处理完一条命令后不关闭，继续读下一条。

### 7.2 写队列

- Session 维护 `std::deque<Buffer>`；
- `Send()` 在 strand 上追加报文；队列原本为空则启动 `async_write`；
- 每次写完成弹出一项，队列非空则继续写；
- 单请求模式下队列通常只有一条响应；写队列为未来长连接的推送复用。

### 7.3 心跳与在线判定

```
Login 成功（TCP 短连接）
  → 服务器登记 OnlineTable，返回 token / heartbeat_interval / heartbeat_timeout
客户端
  → 每 10 秒向服务器 UDP 端口发送一次心跳包（携带 token）
服务器
  → 每次收到合法心跳包，更新 last_heartbeat
  → 定时器每 5 秒扫描：last_heartbeat 超过 30 秒未更新 ⇒ 判定断开
      → 从在线表移除
      → 丢弃该用户的私聊待收队列
```

关键语义：

- **在线与否完全以 UDP 心跳为准**，与 TCP 无关；
- TCP 连接每次使用即断开，不影响在线状态；
- Logout 通过一次 TCP 短连接提交，立即从在线表移除；
- 同一用户名可以重新 Login 获取新 token（如 token 过期），心跳持续则在线条目不中断；
- 伪造或过期的 token 心跳包被丢弃（只记日志），不影响在线状态。

### 7.4 连接生命周期

```
TCP accept
  → 读取一条命令
  → 分发处理（无需先认证：认证本身就是 Register/Login 命令）
  → 写响应
  → 关闭连接
```

统一 `Close(reason)`：

1. 在 strand 上执行并记录日志（含远端地址、原因）；
2. 关闭 socket，清理写队列；Session 由 `shared_from_this` 自然销毁；
3. 在线状态不受连接关闭影响（由心跳维护）。

### 7.5 群聊与历史

群模型：`群 = 服务端管理的命名集合（成员列表 + 内存历史）`，客户端不保存群成员关系。

发送群消息流程：

```
客户端（TCP 短连接）→ SendGroup(group, content)
  → GroupManager 校验：群存在、发送者是成员
  → 分配 seq，追加到群历史（超出上限逐出最旧）
  → 返回 SendGroupResp（含 seq）
```

成员接收：

- 各成员按自己的节奏通过 `PullGroupHistory(group, after_seq, max_count)` 拉取新消息（客户端记录每个群已读到的 seq）；
- 掉线期间错过的消息，重连后按 `after_seq` 追平；
- 服务器按剩余报文空间截断条数，客户端以 `after_seq` 继续翻页；
- 历史只存内存，服务器重启即丢失（符合不持久化原则）。

### 7.6 私聊投递

```
发送方（TCP 短连接）→ SendPrivate(recipient, content)
  → 接收方在线（心跳有效）？否 → 返回“离线”（不缓存，发送方自行决定后续）
  → 是 → 追加到接收方私聊待收队列，返回“已入队”
接收方（TCP 短连接）→ Poll
  → 取出全部待收消息，返回后清空队列
```

语义：

- 待收队列是投递缓冲，不是历史：Poll 一次即清空；
- 接收方心跳超时离线时，其待收队列直接丢弃；
- 队列超出 `max_pending_private` 时逐出最旧消息（记录日志），发送方不感知；
- 发送方客户端如需要确认，可自行与接收方协商（如接收方回复一条私聊）。

### 7.7 错误处理

| 错误类型 | 处理 |
|----------|------|
| 报文格式错误（Length 超限、未知 Type、字段越界） | 返回协议错误后关闭连接 |
| 业务错误（重复注册、密码错误、群不存在等） | 返回对应状态码，连接保持到响应写完 |
| 未认证调用受保护原语 | 返回“未认证”状态码 |
| 网络错误 / EOF | 记录日志并关闭连接；不影响在线状态 |

### 7.8 优雅停机

- `asio::signal_set` 捕获 SIGINT/SIGTERM；
- 停止 accept、关闭 UDP 接收器；
- 等待在途 Session 写完响应后关闭（或超时强关）；
- 停止 io_context，join 所有线程；内存状态随进程退出自然清空。

### 7.9 资源与安全限制（v0.5 加固项）

- 最大连接数（默认 4096）、`max_users`、`max_online`、`max_groups`；
- 单包最大载荷（协议上限 65535 字节，可配置收紧）；
- 单条消息 content 上限（默认 8 KiB，可配置）；
- 每组历史条数上限（默认 1000 条，可配置）；
- 每个在线用户私聊待收队列上限（默认 100 条，可配置）；
- 单请求处理超时（如 30 秒未读完整包则关闭），防止慢连接占用资源；
- 单个 TCP 待写队列深度上限，超限断开。

## 8. 原语 API 概览

详细字段见 [protocol.md](protocol.md)。

| 原语 | 方向 | 说明 |
|------|------|------|
| Register | C→S | 注册用户名/密码 |
| Login | C→S | 校验凭据，签发 token，登记在线，返回心跳参数 |
| Logout | C→S | 主动下线，移除在线条目 |
| OnlineQuery | C→S | 查询一组用户名是否在线（纯心跳判定） |
| SendPrivate | C→S | 私聊：接收方在线则入其待收队列 |
| SendGroup | C→S | 群聊：入历史并返回 seq |
| CreateGroup / JoinGroup / LeaveGroup | C→S | 群管理原语 |
| PullGroupHistory | C→S | 按 seq 增量拉取群聊历史 |
| Poll | C→S | 拉取并清空自己的私聊待收队列 |
| （预留）长连接协商 | C→S | 未来长连接模式使用 |

## 9. 关键设计决策

| 决策点 | 选择 | 理由 |
|--------|------|------|
| 连接模型 | 每个 TCP 连接只处理一条命令，响应后关闭（类似 HTTP） | 需求明确；简化 Session 状态机，天然防粘包/串包 |
| 在线判定 | 纯 UDP 心跳（10s 间隔 / 30s 超时 / 5s 扫描） | TCP 短连接无法承载在线状态；心跳与业务通道解耦 |
| 服务端推送 | 当前不做，客户端轮询拉取 | 短连接模型下无法推送；预留 Push 类型供长连接模式 |
| 私聊投递 | 在线接收方内存待收队列 + Poll | 无历史存储；队列拉取即清空，离线即丢弃 |
| 群聊 | 服务端内存历史 + PullGroupHistory | 支持离线回补，不依赖在线状态 |
| 长连接扩展 | 预留 0x00F0–0x00FF 协商原语与 Push 类型 | 保留后续长连接可能性，不影响当前协议 |
| Length 宽度 | 保持 u16（4 字节头部） | 既定决策；聊天文本场景足够，未来大载荷走分片/扩展头 |
| 群成员 | 服务端管理（加入/离开） | 群历史在服务端，成员关系必须由服务端维护 |
| 认证 | HMAC 自包含 token | 服务器不保存会话凭证；token 内含用户名与过期时间 |
| 共享状态并发 | 单一 logic strand | 正确性优先；状态规模可控，后续再评估细粒度锁 |
| 工程结构 | `bechat_core` 库 + `bechatd` 可执行文件 | 测试与示例直接链接库 |

## 10. 依赖与构建

- 显式声明 `find_package(Asio)`、`find_package(spdlog)`，找不到时 FetchContent 拉取固定版本；
- v0.3 引入 SHA-256/HMAC（推荐 OpenSSL EVP 或等价的 header-only 实现），只使用密码学原语；
- 目录规划：

```
bechat/
  core/            # 库：net/ proto/ service/（核心代码）
  app/             # main.cpp → bechatd
test/              # 单元测试 + 集成测试（CTest）
example/           # 最小客户端示例（TCP 短连接 + UDP 心跳）
```

## 11. 测试策略

- 单元测试：TlvCodec 边界、Dispatcher 路由、token 签发/验证、群历史逐出与 seq、心跳超时判定（用可配置的短超时模拟）；
- 集成测试（真实端口）：
  - 注册 → 登录 → UDP 心跳保活 → 心跳停止 30 秒后离线；
  - 每个 TCP 连接只处理一条命令：第二条命令在同一连接上无效（被忽略或断开）；
  - 私聊：在线接收方 Poll 可拉取且队列清空；离线接收方发送失败且服务器不缓存；心跳超时后队列丢弃；
  - 群聊：发消息 → 历史写入 → 掉线成员重连后按 seq 拉回；
  - 并发：多客户端同时私聊/群聊，无串包、无丢消息；
  - 关闭：SIGINT 优雅退出。

## 12. 明确不做的事（Non-goals）

- 不做持久化与重启恢复（内存状态重启即清空）；
- 私聊消息不做历史保存，只保留在线的短暂待收队列；
- 当前版本不做服务端主动推送、不做长连接（仅预留扩展点）；
- 不引入数据库、缓存、消息队列；
- 不做删群、群成员踢人、群公告等扩展管理（v0.4 仅创建/加入/离开）；
- 不做 TLS（后续演进）；不做多进程/多节点；
- 不做客户端 SDK（客户端协议实现属于 example，不作为交付物）。
