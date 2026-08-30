# 后续开发蓝图（Dispatcher 之后 → v0.0.1 完成）

> 本文档是 `docs/Structrue.md` 的延续：描述从当前代码到 v0.0.1 功能完整的后续计划。
> 只定目标、模块职责和关键决策，不写完整实现；实现细节以代码为准。
>
> 配套文档：
>
> - `docs/DesignLine.md`：功能范围。
> - `docs/TlvProto.md`：线上协议（字段约束、状态码、错误规则都以它为准）。
> - `docs/Structrue.md`：当前代码结构。

## 1. 目标

把 v0.0.1 做完：注册、登录、发送消息、历史拉取、在线列表、两种推送。完成后行为与 `TlvProto.md` 完全一致。

## 2. 非目标（明确不做）

- 持久化 / 数据库、TLS。
- 多聊天室、私聊。
- 请求并发数限制、`request_id` 去重 / 未完成请求表。
- 全局 logic strand（业务 handler 留在 Session 的 write_strand_ 上，见 §4）。
- 历史上限、密码哈希（v0.0.1 按协议明文内存保存）。
- 在线列表分页。

## 3. 新增模块

### 3.1 Session 认证状态 — `core/session.{h,cpp}`

- `SessionState { Unauthenticated, Authenticated }` + `username_`，只在 write_strand_ 上访问。
- `SetAuthenticated(username)`：单向切换（未登录 → 已登录），业务 handler 在 write_strand_ 上直接调用。
- `Send(TlvMessagePtr)`：任意线程可调，post 到 write_strand_ 入队（广播用）。
- 关闭：`Shutdown()` 与 `handle_error()` 殊途同归——置 `closing_`，post 到 write_strand_ 先清理在线状态（已登录时调 `ServerContext::OnSessionClosed`）再关 socket。

### 3.2 业务状态 — 新目录 `service/`

三个容器，由 `ChatService` 的一把 `state_mutex_` 保护，容器自身不加锁；持锁期间不做 socket I/O。

- `UserRegistry`：用户名 → 密码。`Register`（重名返回失败）/ `Verify`。
- `OnlineTable`：用户名 → `weak_ptr<Session>` + 登录时间。只存 weak_ptr，不延长 Session 生命周期。
  - `Put`：添加或顶替，返回被顶替的旧 Session（调用方解锁后再 `Shutdown`）。
  - `RemoveIdentity(username, Session*)`：按 Session 指针比对后才移除，防止旧连接关闭时误删新连接。
  - `Snapshot` / `Sessions`：供在线列表响应和广播取用。
- `HistoryStore`：单聊天室历史。`deque` 存 `ChatRecord{seq, sender, timestamp, content}`，`next_seq_` 从 1 分配。
  - `Append`。
  - `Get(before_seq, limit)`：从后往前取 `seq < before_seq`（`before_seq == 0` 表示不限）的最近 limit 条，再反转为 seq 升序。

### 3.3 ChatService — `service/chat_service.{h,cpp}`

- 5 个 handler：`OnRegister` / `OnLogin` / `OnSendMessage` / `OnGetHistory` / `OnGetOnlineUsers`，签名与当前 `Dispatcher::Dispatch` 的传参风格一致（`DecodedRequest` + `SessionPtr`），返回 `RequestResult`。
- 每个 handler 顺序统一：**解析线格式 → 状态检查 → 语义校验 → 访问共享状态**。
- 另负责 `Broadcast`（锁内复制在线 Session 列表，解锁后逐个 `Send`）与 `OnSessionClosed`。
- 字段约束与常量（用户名/密码/limit/content 长度、`max_payload` 等）一律以 `TlvProto.md` 为准，不另设一套。

### 3.4 Dispatcher 扩展 — `proto/dispatcher.{h,cpp}`

在现有 Tag 校验 + `request_id` 提取之上：

- `Tag → handler` 映射（map 或 switch 均可，5 个已知 Tag）。
- 已知 Tag 但 `request_id == 0`：回对应响应 Tag，`InvalidParameter`（不用 `Resp::Error`）。
- handler 抛异常：捕获后回对应响应 Tag，`InternalError`，连接保持可用。

## 4. 关键决策

1. **业务 handler 在 Session 的 write_strand_ 上同步执行**。`on_read_completed` 的「拷贝 + post」结构不变，只把 `HandleRequest` 里的固定测试响应换成真 handler；同一连接上「接收顺序 == 处理顺序 == 写入顺序」。
2. **共享状态一把 mutex**。三个容器都归 `ChatService::state_mutex_` 管；handler 都很短，不需要更细的锁。未来业务变重再考虑全局 logic strand。
3. **响应先于推送**：`to_self_response` 直接在 write_strand_ 入队，`to_broadcast_push` 经 `Send` post 入队——同一连接上「登录响应先于 UserJoinedPush」「发送响应先于 NewMessagePush」自然成立。
4. **所有权**：`OnlineTable` 存 `weak_ptr<Session>`；广播/顶替时先在锁内取 `shared_ptr`，解锁后再调用 Session 方法。
5. **列表响应截断**：按 `TlvProto.md` §6.4 / §6.5——编码后超过 `max_payload` 时，历史丢最旧条目、在线列表丢登录最早条目，`count` 写实际数量。

## 5. 分阶段实施

### Phase 1：业务状态容器 + ChatService 骨架

- 新建 `service/` 目录：`UserRegistry`、`OnlineTable`、`HistoryStore`、`ChatService`（handler 暂为空）。
- `ChatService` 持 mutex 与三个容器。

验收：编译通过；各容器单元测试通过（注册/Verify、顶替/RemoveIdentity、翻页）。

### Phase 2：Session 认证状态与关闭

- `SessionState` / `username_` / `SetAuthenticated` / `Send` / `Shutdown`；`ServerContext::OnSessionClosed` 链路。
- `handle_error` 与 `Shutdown` 合并为同一条关闭路径。

验收：编译通过；状态切换只发生在 write_strand_ 上。

### Phase 3：注册、登录与 Broadcast

- `OnRegister` / `OnLogin` 接入 Dispatcher；实现 `ServerContext::Broadcast`。
- 登录成功：回 `LoginResp` → 广播 `UserJoinedPush`；重复登录顶替旧连接。

验收：注册 / 登录 / 重复注册 / 错误密码 / 重复登录顶替旧连接；登录者先收 Resp 再收 Push；断线后在线表正确清理。

### Phase 4：聊天、历史与在线列表

- `OnSendMessage` / `OnGetHistory` / `OnGetOnlineUsers`；`NewMessagePush` 广播；两个列表响应的 payload 截断。

验收：发消息后所有在线连接收到 Push；历史翻页正确；在线列表正确。

### Phase 5：协议细节补齐

- 读侧 `max_payload` 校验（超限按 `MalformedPacket` 处理）。
- `request_id == 0` → `InvalidParameter`；UTF-8 校验；`Resp::Error` 各场景按 `TlvProto.md` §7 对齐。

验收：错误注入用例通过（Tag 边界、Value 长度边界、非法 UTF-8、超限包）。

### Phase 6：测试与收尾

- 扩展 `test/TestCMD.md`：注册 / 登录 / 流水线 / 翻页 / 重复登录顶替 / 断线。
- 重点边界：Tag `0x0000` / 未知 Tag / Resp 区间 Tag 当请求发；Value 0 ~ 3 字节；`request_id = 0`；Push 顺序；单条消息接近上限时翻页不漏。
- 复查锁顺序与关闭竞态；补齐日志。

## 6. 待确认的开放问题

1. 密码存储：v0.0.1 明文内存；密码哈希和 TLS 是否 v0.0.2 一起做？
2. 历史上限：当前不限制；是否给 `HistoryStore` 加 `MaxHistorySize`？
3. 在线列表分页：超 payload 时截断且无翻页参数；是否需要分页？
4. 重复登录：旧连接被顶替时，是否向它发一条明确的 Push（当前协议是直接关闭）？
